// SPDX-License-Identifier: GPL-2.0-or-later

#include "d3d12_status_renderer.h"

#include <dxcapi.h>
#include <winrt/base.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr char TriangleShader[] = R"(
struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOutput VSMain(uint vertex_id : SV_VertexID) {
    const float2 positions[3] = {
        float2( 0.0,  0.65),
        float2( 0.65, -0.55),
        float2(-0.65, -0.55)
    };
    const float3 colors[3] = {
        float3(0.15, 1.0, 0.35),
        float3(0.10, 0.55, 1.0),
        float3(1.0, 0.20, 0.15)
    };

    VertexOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.color = colors[vertex_id];
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
)";

} // namespace

using Microsoft::WRL::ComPtr;

D3D12StatusRenderer::~D3D12StatusRenderer() {
    if (fence_event_ != INVALID_HANDLE_VALUE) {
        CloseHandle(fence_event_);
    }
}

void D3D12StatusRenderer::Initialize(IUnknown* core_window, float width, float height) {
    winrt::check_hresult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                          IID_PPV_ARGS(device_.ReleaseAndGetAddressOf())));

    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    winrt::check_hresult(device_->CreateCommandQueue(
        &queue_description, IID_PPV_ARGS(command_queue_.ReleaseAndGetAddressOf())));

    ComPtr<IDXGIFactory4> factory;
    winrt::check_hresult(
        CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())));

    DXGI_SWAP_CHAIN_DESC1 swap_chain_description{};
    swap_chain_description.Width = std::max(1U, static_cast<UINT>(width));
    swap_chain_description.Height = std::max(1U, static_cast<UINT>(height));
    swap_chain_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_chain_description.SampleDesc.Count = 1;
    swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_description.BufferCount = FrameCount;
    swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_chain_description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swap_chain;
    winrt::check_hresult(factory->CreateSwapChainForCoreWindow(
        command_queue_.Get(), core_window, &swap_chain_description, nullptr,
        swap_chain.ReleaseAndGetAddressOf()));
    winrt::check_hresult(swap_chain.As(&swap_chain_));

    viewport_.Width = static_cast<float>(swap_chain_description.Width);
    viewport_.Height = static_cast<float>(swap_chain_description.Height);
    viewport_.MaxDepth = 1.0F;
    scissor_.right = static_cast<LONG>(swap_chain_description.Width);
    scissor_.bottom = static_cast<LONG>(swap_chain_description.Height);

    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.NumDescriptors = FrameCount;
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    winrt::check_hresult(device_->CreateDescriptorHeap(
        &heap_description, IID_PPV_ARGS(rtv_heap_.ReleaseAndGetAddressOf())));
    rtv_descriptor_size_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    auto rtv_handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < FrameCount; ++index) {
        winrt::check_hresult(swap_chain_->GetBuffer(
            index, IID_PPV_ARGS(render_targets_[index].ReleaseAndGetAddressOf())));
        device_->CreateRenderTargetView(render_targets_[index].Get(), nullptr, rtv_handle);
        rtv_handle.ptr += rtv_descriptor_size_;
    }

    winrt::check_hresult(device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(command_allocator_.ReleaseAndGetAddressOf())));
    winrt::check_hresult(device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator_.Get(), nullptr,
        IID_PPV_ARGS(command_list_.ReleaseAndGetAddressOf())));
    winrt::check_hresult(command_list_->Close());

    winrt::check_hresult(
        device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                             IID_PPV_ARGS(fence_.ReleaseAndGetAddressOf())));
    fence_event_ = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (fence_event_ == nullptr) {
        fence_event_ = INVALID_HANDLE_VALUE;
        winrt::throw_last_error();
    }

    CreateTrianglePipeline();
}

void D3D12StatusRenderer::Render(bool passed) {
    winrt::check_hresult(command_allocator_->Reset());
    winrt::check_hresult(command_list_->Reset(
        command_allocator_.Get(), passed ? pipeline_state_.Get() : nullptr));

    const UINT frame_index = swap_chain_->GetCurrentBackBufferIndex();
    D3D12_RESOURCE_BARRIER begin_barrier{};
    begin_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    begin_barrier.Transition.pResource = render_targets_[frame_index].Get();
    begin_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    begin_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    begin_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list_->ResourceBarrier(1, &begin_barrier);

    auto rtv_handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv_handle.ptr += static_cast<SIZE_T>(frame_index) * rtv_descriptor_size_;
    constexpr float pass_color[] = {0.015F, 0.025F, 0.055F, 1.0F};
    constexpr float fail_color[] = {0.62F, 0.03F, 0.03F, 1.0F};
    command_list_->ClearRenderTargetView(rtv_handle, passed ? pass_color : fail_color, 0, nullptr);
    if (passed) {
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        command_list_->RSSetViewports(1, &viewport_);
        command_list_->RSSetScissorRects(1, &scissor_);
        command_list_->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->DrawInstanced(3, 1, 0, 0);
    }

    D3D12_RESOURCE_BARRIER end_barrier = begin_barrier;
    end_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    end_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    command_list_->ResourceBarrier(1, &end_barrier);

    winrt::check_hresult(command_list_->Close());
    ID3D12CommandList* command_lists[] = {command_list_.Get()};
    command_queue_->ExecuteCommandLists(1, command_lists);
    winrt::check_hresult(swap_chain_->Present(1, 0));
    WaitForGpu();
}

void D3D12StatusRenderer::Trim() {
    ComPtr<IDXGIDevice3> dxgi_device;
    winrt::check_hresult(device_.As(&dxgi_device));
    dxgi_device->Trim();
}

void D3D12StatusRenderer::CreateTrianglePipeline() {
    ComPtr<IDxcLibrary> library;
    ComPtr<IDxcCompiler> compiler;
    winrt::check_hresult(
        DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(library.ReleaseAndGetAddressOf())));
    winrt::check_hresult(
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.ReleaseAndGetAddressOf())));

    ComPtr<IDxcBlobEncoding> source;
    winrt::check_hresult(library->CreateBlobWithEncodingFromPinned(
        TriangleShader, static_cast<UINT32>(std::strlen(TriangleShader)), DXC_CP_UTF8,
        source.ReleaseAndGetAddressOf()));
    const auto compile_shader = [&](const wchar_t* entry_point,
                                    const wchar_t* target) -> ComPtr<IDxcBlob> {
        const wchar_t* arguments[] = {L"-Ges", L"-O3"};
        ComPtr<IDxcOperationResult> result;
        winrt::check_hresult(compiler->Compile(
            source.Get(), L"triangle.hlsl", entry_point, target, arguments, 2, nullptr, 0,
            nullptr, result.ReleaseAndGetAddressOf()));
        HRESULT status = E_FAIL;
        winrt::check_hresult(result->GetStatus(&status));
        winrt::check_hresult(status);

        ComPtr<IDxcBlob> shader;
        winrt::check_hresult(result->GetResult(shader.ReleaseAndGetAddressOf()));
        return shader;
    };

    const ComPtr<IDxcBlob> vertex_shader = compile_shader(L"VSMain", L"vs_6_0");
    const ComPtr<IDxcBlob> pixel_shader = compile_shader(L"PSMain", L"ps_6_0");

    D3D12_ROOT_SIGNATURE_DESC root_description{};
    root_description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serialized_root;
    ComPtr<ID3DBlob> diagnostics;
    winrt::check_hresult(D3D12SerializeRootSignature(
        &root_description, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized_root.ReleaseAndGetAddressOf(), diagnostics.ReleaseAndGetAddressOf()));
    winrt::check_hresult(device_->CreateRootSignature(
        0, serialized_root->GetBufferPointer(), serialized_root->GetBufferSize(),
        IID_PPV_ARGS(root_signature_.ReleaseAndGetAddressOf())));

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC depth_stencil{};
    depth_stencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth_stencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depth_stencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depth_stencil.BackFace = depth_stencil.FrontFace;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = root_signature_.Get();
    pipeline.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
    pipeline.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    pipeline.BlendState = blend;
    pipeline.SampleMask = UINT_MAX;
    pipeline.RasterizerState = rasterizer;
    pipeline.DepthStencilState = depth_stencil;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    pipeline.SampleDesc.Count = 1;
    winrt::check_hresult(device_->CreateGraphicsPipelineState(
        &pipeline, IID_PPV_ARGS(pipeline_state_.ReleaseAndGetAddressOf())));
}

void D3D12StatusRenderer::WaitForGpu() {
    const UINT64 value = ++fence_value_;
    winrt::check_hresult(command_queue_->Signal(fence_.Get(), value));
    if (fence_->GetCompletedValue() >= value) {
        return;
    }

    winrt::check_hresult(fence_->SetEventOnCompletion(value, fence_event_));
    if (WaitForSingleObjectEx(fence_event_, INFINITE, FALSE) == WAIT_FAILED) {
        winrt::throw_last_error();
    }
}
