// SPDX-License-Identifier: GPL-2.0-or-later
#include "d3d12_compute_probe.h"
#include "spirv_hlsl_bridge.h"
#include "upstream_compute.h"

#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <dxgi1_4.h>
#include <winrt/base.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Action>
void Reject(Action action, HRESULT expected) {
    try {
        action();
    } catch (const std::bad_alloc&) {
        // C++/WinRT translates E_OUTOFMEMORY into std::bad_alloc.
        Check(expected == E_OUTOFMEMORY, "unexpected allocation exception");
        return;
    } catch (const winrt::hresult_error& error) {
        Check(error.code().value == expected, "unexpected rejection HRESULT");
        return;
    }
    throw std::runtime_error("invalid request was accepted");
}

ComPtr<ID3DBlob> Compile(const char* source, const char* entry, const char* target) {
    ComPtr<ID3DBlob> result;
    ComPtr<ID3DBlob> errors;
    const HRESULT status =
        D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, target,
                   D3DCOMPILE_ENABLE_STRICTNESS, 0, result.ReleaseAndGetAddressOf(),
                   errors.ReleaseAndGetAddressOf());
    if (FAILED(status) && errors) {
        std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
                        static_cast<std::streamsize>(errors->GetBufferSize()));
    }
    winrt::check_hresult(status);
    return result;
}

D3D12_SHADER_BYTECODE Bytes(ID3DBlob* blob) {
    return {blob->GetBufferPointer(), blob->GetBufferSize()};
}

ComPtr<IDxcBlob> CompileDxil(const std::string& text) {
    ComPtr<IDxcLibrary> library;
    ComPtr<IDxcCompiler> compiler;
    winrt::check_hresult(
        DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(library.ReleaseAndGetAddressOf())));
    winrt::check_hresult(
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.ReleaseAndGetAddressOf())));
    ComPtr<IDxcBlobEncoding> source;
    winrt::check_hresult(
        library->CreateBlobWithEncodingFromPinned(text.data(), static_cast<UINT32>(text.size()),
                                                  DXC_CP_UTF8, source.ReleaseAndGetAddressOf()));
    const wchar_t* arguments[]{L"-Ges", L"-O3"};
    ComPtr<IDxcOperationResult> operation;
    winrt::check_hresult(compiler->Compile(source.Get(), L"upstream_probe.hlsl", L"main", L"cs_6_0",
                                           arguments, 2, nullptr, 0, nullptr,
                                           operation.ReleaseAndGetAddressOf()));
    HRESULT status{};
    winrt::check_hresult(operation->GetStatus(&status));
    if (FAILED(status)) {
        ComPtr<IDxcBlobEncoding> errors;
        winrt::check_hresult(operation->GetErrorBuffer(errors.ReleaseAndGetAddressOf()));
        if (errors) {
            std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
                            static_cast<std::streamsize>(errors->GetBufferSize()));
        }
        winrt::check_hresult(status);
    }
    ComPtr<IDxcBlob> result;
    winrt::check_hresult(operation->GetResult(result.ReleaseAndGetAddressOf()));
    return result;
}

D3D12_SHADER_BYTECODE Bytes(IDxcBlob* blob) {
    return {blob->GetBufferPointer(), blob->GetBufferSize()};
}

void TestDescriptors(ID3D12Device* device) {
    D3D12DescriptorArena arena;
    Reject([&] { (void)arena.Allocate(); }, E_INVALIDARG);
    Reject([&] { arena.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, true); },
           E_INVALIDARG);
    arena.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
    Reject([&] { (void)arena.Cpu(0); }, E_INVALIDARG);
    Check(arena.Allocate(2) == 0 && arena.Used() == 2, "slot reservation differs");
    Check(arena.Cpu(1).ptr - arena.Cpu(0).ptr ==
              device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
          "descriptor increment differs");
    Reject([&] { (void)arena.Allocate(); }, E_OUTOFMEMORY);
    Reject([&] { (void)arena.Allocate(0); }, E_INVALIDARG);
    Reject([&] { (void)arena.Cpu(2); }, E_INVALIDARG);
    Reject([&] { (void)arena.Gpu(0); }, E_INVALIDARG);
    Check(arena.Used() == 2, "failed reservation changed the arena");
}

void TestRootLimit(ID3D12Device* device) {
    D3D12PipelineCache cache;
    cache.Initialize(device);
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    for (UINT count = 1; count <= 64; ++count) {
        parameter.Constants.Num32BitValues = count;
        (void)cache.RootSignature(desc);
    }
    Check(cache.Stats().root_creations == 64, "root cache limit differs");
    Reject([&] { (void)cache.RootSignature({}); }, E_OUTOFMEMORY);
    // Hits remain usable even when the cache is full.
    (void)cache.RootSignature(desc);
    Check(cache.Stats().cache_hits == 1, "full cache rejected an existing entry");
}

void TestGraphics(D3D12DeviceContext& context, D3D12ResourceAllocator& allocator,
                  D3D12PipelineCache& cache) {
    constexpr char shader[] = R"(
float4 VSMain(uint id : SV_VertexID) : SV_Position {
  float2 p[3] = {float2(-1,-1), float2(-1,3), float2(3,-1)};
  return float4(p[id], 0, 1);
}
float4 PSMain() : SV_Target { return float4(1, 0, 0, 1); }
float4 PSOther() : SV_Target { return float4(0, 1, 0, 1); }
)";
    const auto vertex = Compile(shader, "VSMain", "vs_5_0");
    const auto pixel = Compile(shader, "PSMain", "ps_5_0");
    const auto other = Compile(shader, "PSOther", "ps_5_0");
    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    const auto root = cache.RootSignature(root_desc);
    Check(cache.RootSignature(root_desc) == root, "root cache miss for same layout");
    const auto pipeline = cache.Graphics(root, Bytes(vertex.Get()), Bytes(pixel.Get()),
                                         DXGI_FORMAT_R8G8B8A8_UNORM, false);
    const auto* start = static_cast<const std::uint8_t*>(pixel->GetBufferPointer());
    std::vector<std::uint8_t> relocated(start, start + pixel->GetBufferSize());
    Check(cache.Graphics(root, Bytes(vertex.Get()), {relocated.data(), relocated.size()},
                         DXGI_FORMAT_R8G8B8A8_UNORM, false)
                  .Get() == pipeline.Get(),
          "byte-identical shader at different address missed cache");
    Check(cache.Graphics(root, Bytes(vertex.Get()), Bytes(other.Get()), DXGI_FORMAT_R8G8B8A8_UNORM,
                         false)
                  .Get() != pipeline.Get(),
          "changed shader reused pipeline");
    Check(cache.Graphics(root, Bytes(vertex.Get()), Bytes(pixel.Get()), DXGI_FORMAT_R8G8B8A8_UNORM,
                         true)
                  .Get() != pipeline.Get(),
          "changed blend state reused pipeline");
    Check(cache.Graphics(root, Bytes(vertex.Get()), Bytes(pixel.Get()), DXGI_FORMAT_B8G8R8A8_UNORM,
                         false)
                  .Get() != pipeline.Get(),
          "changed color format reused pipeline");
    D3D12PipelineCache foreign;
    foreign.Initialize(context.Device());
    const auto foreign_root = foreign.RootSignature(root_desc);
    Reject(
        [&] {
            (void)cache.Graphics(foreign_root, Bytes(vertex.Get()), Bytes(pixel.Get()),
                                 DXGI_FORMAT_R8G8B8A8_UNORM, false);
        },
        E_INVALIDARG);
    Reject([&] { (void)cache.Compute(root, {}); }, E_INVALIDARG);

    D3D12_RESOURCE_DESC target_desc{};
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target_desc.Width = 4;
    target_desc.Height = 4;
    target_desc.DepthOrArraySize = 1;
    target_desc.MipLevels = 1;
    target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.SampleDesc.Count = 1;
    target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12Resource target;
    winrt::check_hresult(
        allocator.CreateTexture2D(target_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, target));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 size{};
    context.Device()->GetCopyableFootprints(&target_desc, 0, 1, 0, &footprint, nullptr, nullptr,
                                            &size);
    D3D12Resource readback;
    winrt::check_hresult(allocator.CreateBuffer(size, D3D12_HEAP_TYPE_READBACK,
                                                D3D12_RESOURCE_STATE_COPY_DEST, readback));
    D3D12DescriptorArena rtv;
    rtv.Initialize(context.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    const auto handle = rtv.Cpu(rtv.Allocate());
    context.Device()->CreateRenderTargetView(target.Get(), nullptr, handle);
    ComPtr<ID3D12CommandAllocator> commands_allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    context.CreateDirectCommands(commands_allocator, commands);
    commands->SetPipelineState(pipeline.Get());
    commands->SetGraphicsRootSignature(root->Get());
    const D3D12_VIEWPORT viewport{0, 0, 4, 4, 0, 1};
    const D3D12_RECT scissor{0, 0, 4, 4};
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    commands->OMSetRenderTargets(1, &handle, FALSE, nullptr);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3, 1, 0, 0);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = target.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    winrt::check_hresult(commands->Close());
    context.Wait(context.Submit(commands.Get()));
    void* mapped{};
    const D3D12_RANGE range{0, static_cast<SIZE_T>(size)};
    winrt::check_hresult(readback->Map(0, &range, &mapped));
    bool passed = true;
    for (UINT y = 0; y < 4; ++y) {
        for (UINT x = 0; x < 4; ++x) {
            const auto* pixel_bytes = static_cast<const std::uint8_t*>(mapped) + footprint.Offset +
                                      y * footprint.Footprint.RowPitch + x * 4;
            passed = passed && pixel_bytes[0] == 255 && pixel_bytes[1] == 0 &&
                     pixel_bytes[2] == 0 && pixel_bytes[3] == 255;
        }
    }
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
    Check(passed, "graphics draw/readback pixel mismatch");
    Check(cache.Stats().graphics_creations == 4, "graphics cache creation count differs");
}
} // namespace

int main() {
    try {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.ReleaseAndGetAddressOf())))) {
            debug->EnableDebugLayer();
        }
        ComPtr<IDXGIFactory4> factory;
        winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())));
        ComPtr<IDXGIAdapter> warp;
        winrt::check_hresult(factory->EnumWarpAdapter(IID_PPV_ARGS(warp.ReleaseAndGetAddressOf())));
        D3D12DeviceContext context;
        context.Initialize(warp.Get());
        TestDescriptors(context.Device());
        TestRootLimit(context.Device());
        D3D12ResourceAllocator allocator;
        allocator.Initialize(context.Device(), 4ULL * 1024ULL * 1024ULL);
        D3D12PipelineCache cache;
        cache.Initialize(context.Device());
        TestGraphics(context, allocator, cache);
        const auto shader = Compile(D3D12ComputeProbeShader, "CSMain", "cs_5_0");
        Check(RunD3D12ComputeProbe(context, allocator, cache, Bytes(shader.Get())),
              "compute dispatch/readback values differ");
        const auto emitted = Xbox::Shaders::TranslateCompute(Xbox::Shaders::EmitUpstreamCompute());
        // Match the Xbox production compiler and flags. FXC/DXBC folds this
        // integer -> float-vector -> integer bit carrier into zero stores.
        const auto emitted_shader = CompileDxil(emitted.hlsl);
        const auto emitted_push = Xbox::Shaders::EncodePushData(Shader::PushData{});
        Check(emitted.push_constant_words == emitted_push.size(), "upstream PushData reflection");
        const bool emitted_passed = RunD3D12ComputeProbe(
            context, allocator, cache, Bytes(emitted_shader.Get()), nullptr, emitted_push);
        if (!emitted_passed) {
            std::cerr << emitted.hlsl << '\n';
        }
        Check(emitted_passed, "upstream EmitSPIRV readback mismatch");
        const auto changed_emitted =
            Xbox::Shaders::TranslateCompute(Xbox::Shaders::EmitUpstreamCompute(200));
        const auto changed_emitted_shader = CompileDxil(changed_emitted.hlsl);
        Check(!RunD3D12ComputeProbe(context, allocator, cache, Bytes(changed_emitted_shader.Get()),
                                    nullptr, emitted_push),
              "upstream IR mutation accepted by fixed oracle");
        Check(RunD3D12ComputeProbe(context, allocator, cache, Bytes(changed_emitted_shader.Get()),
                                   nullptr, emitted_push, 200),
              "upstream IR mutation did not reach GPU");
        const auto translated = Xbox::Shaders::TranslateCompute(Xbox::Shaders::ComputeFixture());
        const auto translated_shader = Compile(translated.hlsl.c_str(), "main", "cs_5_1");
        Check(RunD3D12ComputeProbe(context, allocator, cache, Bytes(translated_shader.Get())),
              "SPIR-V/HLSL compute readback values differ");
        auto changed_words = Xbox::Shaders::ComputeFixture();
        for (std::size_t offset = 5; offset < changed_words.size();) {
            const auto count = changed_words[offset] >> 16;
            if ((changed_words[offset] & 0xffff) == 43 && count == 4 &&
                changed_words[offset + 3] == 100) {
                changed_words[offset + 3] = 200;
            }
            offset += count;
        }
        const auto changed_translation = Xbox::Shaders::TranslateCompute(changed_words);
        const auto changed_shader = Compile(changed_translation.hlsl.c_str(), "main", "cs_5_1");
        Check(!RunD3D12ComputeProbe(context, allocator, cache, Bytes(changed_shader.Get())),
              "readback oracle accepted deliberately wrong shader values");
        const auto push_translation =
            Xbox::Shaders::TranslateCompute(Xbox::Shaders::PushDataFixture());
        const auto push_shader = Compile(push_translation.hlsl.c_str(), "main", "cs_5_1");
        const auto push_words = Xbox::Shaders::EncodePushData(Xbox::Shaders::ProbePushData());
        Check(push_translation.push_constant_words == push_words.size(),
              "PushData root size mismatch");
        Check(RunD3D12ComputeProbe(context, allocator, cache, Bytes(push_shader.Get()), nullptr,
                                   push_words, 1066),
              "PushData full ABI readback mismatch");
        Shader::PushData edge_data{};
        edge_data.xoffset = 1;
        edge_data.ud_regs[15] = 9;
        edge_data.buf_offsets[39] = 7;
        const auto edge_words = Xbox::Shaders::EncodePushData(edge_data);
        Check(RunD3D12ComputeProbe(context, allocator, cache, Bytes(push_shader.Get()), nullptr,
                                   edge_words, 117),
              "PushData last register/packed byte or root update mismatch");
        Check(!RunD3D12ComputeProbe(context, allocator, cache, Bytes(push_shader.Get()), nullptr,
                                    edge_words, 1066),
              "PushData oracle accepted stale values");
        Reject(
            [&] {
                (void)RunD3D12ComputeProbe(context, allocator, cache, Bytes(push_shader.Get()),
                                           nullptr, std::span(push_words).first(29), 1066);
            },
            E_INVALIDARG);
        Check(allocator.Stats().live_resources == 0 && allocator.Stats().live_bytes == 0,
              "test resources leaked");
        Check(cache.Stats().compute_creations == 6 && cache.Stats().cache_hits >= 10,
              "compute cache did not reuse the PSO");
        ComPtr<ID3D12InfoQueue> queue;
        if (SUCCEEDED(
                context.Device()->QueryInterface(IID_PPV_ARGS(queue.ReleaseAndGetAddressOf())))) {
            for (UINT64 index = 0; index < queue->GetNumStoredMessages(); ++index) {
                SIZE_T size{};
                winrt::check_hresult(queue->GetMessage(index, nullptr, &size));
                std::vector<std::uint8_t> storage(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                winrt::check_hresult(queue->GetMessage(index, message, &size));
                Check(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                          message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION,
                      message->pDescription);
            }
        }
        std::cout << "D3D12 descriptor/root/PSO cache, graphics pixels and compute values passed\n";
        return 0;
    } catch (const winrt::hresult_error& error) {
        std::cerr << "HRESULT: " << static_cast<unsigned long>(error.code().value) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
