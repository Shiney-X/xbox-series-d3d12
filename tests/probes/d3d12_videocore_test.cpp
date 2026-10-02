// SPDX-License-Identifier: GPL-2.0-or-later
#include "d3d12_videocore_probe.h"
#include "d3d12_pipeline_cache.h"

#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <winrt/base.h>

#include <concepts>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
static_assert(std::derived_from<D3D12VideoCoreBridge, VideoCore::GpuCommandSink>);

namespace {
void Check(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Action> void Reject(Action action, HRESULT expected = E_INVALIDARG) {
  try {
    action();
  } catch (const winrt::hresult_error &error) {
    Check(error.code().value == expected, "wrong bridge rejection HRESULT");
    return;
  }
  throw std::runtime_error("invalid/unsupported VideoCore request accepted");
}

void TestBoundary(D3D12DeviceContext &context) {
  D3D12ResourceAllocator allocator;
  allocator.Initialize(context.Device(), 4ULL * 1024ULL * 1024ULL);
  D3D12Resource source;
  D3D12Resource destination;
  D3D12Resource upload;
  winrt::check_hresult(allocator.CreateBuffer(1024, D3D12_HEAP_TYPE_DEFAULT,
                                              D3D12_RESOURCE_STATE_COMMON, source));
  winrt::check_hresult(allocator.CreateBuffer(1024, D3D12_HEAP_TYPE_DEFAULT,
                                              D3D12_RESOURCE_STATE_COMMON, destination));
  winrt::check_hresult(allocator.CreateBuffer(1024, D3D12_HEAP_TYPE_UPLOAD,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, upload));
  D3D12TransferStats transfers;
  D3D12VideoCoreBridge bridge(context, allocator, transfers);
  VideoCore::GpuCommandSink &sink = bridge;
  const UINT64 submitted_before = context.SubmittedLists();
  Check(sink.Flush() == 0, "empty Flush created work or a fake ticket");
  Reject([&] { bridge.RegisterBuffer(0, source.Get()); });
  Reject([&] { bridge.RegisterBuffer(0x10000, nullptr); });
  Reject([&] { bridge.RegisterBuffer(std::numeric_limits<VAddr>::max() - 127, source.Get()); });
  Reject([&] { bridge.RegisterBuffer(0x30000, upload.Get()); });
  bridge.RegisterBuffer(0x10000, source.Get());
  Reject([&] { bridge.RegisterBuffer(0x10080, destination.Get()); });
  Reject([&] { bridge.RegisterBuffer(0x30000, source.Get()); });
  bridge.RegisterBuffer(0x20000, destination.Get());
  Reject([&] { sink.FillBuffer(0x999, 4, 1, false); });
  Reject([&] { sink.FillBuffer(0x10000, 0, 1, false); });
  Reject([&] { sink.FillBuffer(0x10001, 4, 1, false); });
  Reject([&] { sink.FillBuffer(0x10000, 3, 1, false); });
  Reject([&] { sink.FillBuffer(0x103FE, 4, 1, false); });
  Reject([&] { sink.CopyBuffer(0x20000, std::numeric_limits<VAddr>::max(), 4, false, false); });
  Reject([&] { sink.CopyBuffer(0x10004, 0x10000, 4, false, false); });
  D3D12Resource frame;
  Reject([&] { bridge.CreateLinearFrame(0x20000, 63, 1, 252, frame); });
  Reject([&] { bridge.CreateLinearFrame(0x20004, 64, 1, 256, frame); });
  Check(!frame.Get(), "failed frame import changed output ownership");
  Reject([&] { sink.ScopeMarkerEnd(); });
  Reject([&] { sink.ScopeMarkerBegin(""); });
  Reject([&] { sink.Draw(false); }, E_NOTIMPL);
  Reject([&] { sink.DrawIndirect(false, 0, 0, 0, 0, 0); }, E_NOTIMPL);
  Reject([&] { sink.DispatchDirect(); }, E_NOTIMPL);
  Reject([&] { sink.DispatchIndirect(0, 0, 0); }, E_NOTIMPL);
  Reject([&] { (void)sink.ReadDataFromGds(0); }, E_NOTIMPL);
  Reject([&] { sink.ProcessDownloadImages(); }, E_NOTIMPL);
  Reject([&] { sink.FillBuffer(0x10000, 4, 1, true); }, E_NOTIMPL);
  Reject([&] { sink.CopyBuffer(0x20000, 0x10000, 4, false, true); }, E_NOTIMPL);
  Reject([&] { sink.CopyBuffer(0x20000, 0x10000, 4, true, false); }, E_NOTIMPL);
  Check(context.SubmittedLists() == submitted_before && transfers.transitions == 0 &&
            bridge.Stats().unsupported_requests == 9,
        "invalid/unsupported request submitted GPU work");
  sink.ScopeMarkerBegin("partial-range transfer");
  Reject([&] { (void)sink.Flush(); });
  Reject([&] { bridge.RegisterBuffer(0x40000, source.Get()); });
  sink.FillBuffer(0x10008, 8, 12345, false);
  sink.CopyBuffer(0x20014, 0x10008, 8, false, false);
  sink.OnSubmit();
  Check(allocator.Stats().upload_bytes > upload.AllocationBytes(),
        "OnSubmit released staging from an unsubmitted recording");
  sink.ScopeMarkerEnd();
  const u64 ticket = sink.Flush();
  Check(ticket != 0 && sink.Flush() == ticket &&
            context.SubmittedLists() == submitted_before + 1,
        "real/repeated Flush ticket semantics differ");
  Check(allocator.Stats().upload_bytes > upload.AllocationBytes(),
        "Flush released staging before fence wait");
  sink.Finish();
  Check(allocator.Stats().upload_bytes == upload.AllocationBytes(),
        "Finish did not release completed staging");
  std::vector<std::uint8_t> readback(8);
  bridge.DownloadBuffer(0x20014, readback);
  for (std::size_t offset = 0; offset < readback.size(); offset += sizeof(u32)) {
    u32 value{};
    std::memcpy(&value, readback.data() + offset, sizeof(value));
    Check(value == 12345, "registered partial range copy/readback differs");
  }
}

void TestDiscard(D3D12DeviceContext &context) {
  D3D12ResourceAllocator allocator;
  allocator.Initialize(context.Device(), 1024ULL * 1024ULL);
  D3D12Resource buffer;
  winrt::check_hresult(allocator.CreateBuffer(64, D3D12_HEAP_TYPE_DEFAULT,
                                              D3D12_RESOURCE_STATE_COMMON, buffer));
  D3D12TransferStats transfers;
  const UINT64 submitted = context.SubmittedLists();
  {
    D3D12VideoCoreBridge bridge(context, allocator, transfers);
    bridge.RegisterBuffer(0x10000, buffer.Get());
    VideoCore::GpuCommandSink &sink = bridge;
    sink.FillBuffer(0x10000, 4, 42, false);
  }
  Check(context.SubmittedLists() == submitted && allocator.Stats().upload_bytes == 0,
        "destructor submitted a discarded recording or leaked staging");
}

ComPtr<ID3DBlob> Compile(const char *source, const char *entry, const char *target) {
  ComPtr<ID3DBlob> result;
  ComPtr<ID3DBlob> errors;
  winrt::check_hresult(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
                                 entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                 result.ReleaseAndGetAddressOf(), errors.ReleaseAndGetAddressOf()));
  return result;
}

void TestFrameSampling(D3D12DeviceContext &context, D3D12ResourceAllocator &allocator,
                       ID3D12Resource *frame) {
  constexpr char shader[] = R"(
Texture2D<float4> image : register(t0);
SamplerState point_sampler : register(s0);
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VSMain(uint id : SV_VertexID) {
  float2 p[3] = {float2(0,0),float2(0,2),float2(2,0)};
  Vertex output;
  output.position = float4(p[id] * float2(2,-2) + float2(-1,1),0,1);
  output.uv = p[id]; return output;
}
float4 PSMain(Vertex input) : SV_Target { return image.Sample(point_sampler,input.uv); }
)";
  auto desc = frame->GetDesc();
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12Resource target;
  winrt::check_hresult(allocator.CreateTexture2D(desc, D3D12_RESOURCE_STATE_COMMON, target));
  D3D12DescriptorArena rtv;
  rtv.Initialize(context.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  const auto rtv_handle = rtv.Cpu(rtv.Allocate());
  context.Device()->CreateRenderTargetView(target.Get(), nullptr, rtv_handle);
  D3D12DescriptorArena srv;
  srv.Initialize(context.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  const auto srv_slot = srv.Allocate();
  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = desc.Format;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  context.Device()->CreateShaderResourceView(frame, &view, srv.Cpu(srv_slot));
  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameter{};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  D3D12_STATIC_SAMPLER_DESC sampler{};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  root_desc.NumStaticSamplers = 1;
  root_desc.pStaticSamplers = &sampler;
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  D3D12PipelineCache cache;
  cache.Initialize(context.Device());
  const auto root = cache.RootSignature(root_desc);
  const auto vertex = Compile(shader, "VSMain", "vs_5_0");
  const auto pixel = Compile(shader, "PSMain", "ps_5_0");
  const auto pipeline = cache.Graphics(root,
      {vertex->GetBufferPointer(), vertex->GetBufferSize()},
      {pixel->GetBufferPointer(), pixel->GetBufferSize()}, desc.Format, false);
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 bytes{};
  context.Device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
  D3D12Resource readback;
  winrt::check_hresult(allocator.CreateBuffer(bytes, D3D12_HEAP_TYPE_READBACK,
                                              D3D12_RESOURCE_STATE_COPY_DEST, readback));
  ComPtr<ID3D12CommandAllocator> commands_allocator;
  ComPtr<ID3D12GraphicsCommandList> commands;
  context.CreateDirectCommands(commands_allocator, commands);
  D3D12TransferStats transfers;
  D3D12CommandEncoder encoder(commands.Get(), transfers);
  encoder.Track(target.Get(), D3D12_RESOURCE_STATE_COMMON);
  encoder.Track(readback.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
  encoder.Transition(target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
  commands->SetPipelineState(pipeline.Get());
  commands->SetGraphicsRootSignature(root->Get());
  ID3D12DescriptorHeap *heaps[]{srv.Heap()};
  commands->SetDescriptorHeaps(1, heaps);
  commands->SetGraphicsRootDescriptorTable(0, srv.Gpu(srv_slot));
  const D3D12_VIEWPORT viewport{0, 0, 64, 32, 0, 1};
  const D3D12_RECT scissor{0, 0, 64, 32};
  commands->RSSetViewports(1, &viewport);
  commands->RSSetScissorRects(1, &scissor);
  commands->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
  commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  commands->DrawInstanced(3, 1, 0, 0);
  encoder.CopyTextureToBuffer(readback.Get(), target.Get(), footprint);
  winrt::check_hresult(commands->Close());
  context.Wait(context.Submit(commands.Get()));
  void *mapped{};
  const D3D12_RANGE read_range{0, static_cast<SIZE_T>(bytes)};
  winrt::check_hresult(readback->Map(0, &read_range, &mapped));
  bool passed = true;
  for (UINT y = 0; y < 32; ++y) {
    for (UINT x = 0; x < 64; ++x) {
      u32 value{};
      const auto *address = static_cast<const std::uint8_t *>(mapped) + footprint.Offset +
                            y * footprint.Footprint.RowPitch + x * sizeof(u32);
      std::memcpy(&value, address, sizeof(value));
      passed = passed && value == (y < 16 ? 0xFF30D070U : 0xFFFFA030U);
    }
  }
  const D3D12_RANGE no_write{0, 0};
  readback->Unmap(0, &no_write);
  Check(passed, "sampling VideoCore frame into color target changed pixels");
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
    TestBoundary(context);
    TestDiscard(context);
    D3D12ResourceAllocator allocator;
    allocator.Initialize(context.Device(), 16ULL * 1024ULL * 1024ULL);
    D3D12TransferStats transfers;
    auto probe = RunD3D12VideoCoreProbe(context, allocator, transfers);
    Check(probe.Passed() && probe.dma_ticket != 0 && probe.frame_ticket > probe.dma_ticket &&
              context.CompletedValue() >= probe.frame_ticket,
          "VideoCore DMA/linear-frame readback probe failed");
    const auto &stats = probe.stats;
    Check(stats.fills == 2 && stats.copies == 1 && stats.flushes == 3 &&
              stats.synchronizations == 4 && stats.downloads == 1 && stats.linear_frames == 1 &&
              stats.host_submit_events == 2 && stats.markers == 2,
          "VideoCore interface counters differ");
    TestFrameSampling(context, allocator, probe.frame.Get());
    Check(allocator.Stats().live_resources == 1 && allocator.Stats().upload_bytes == 0 &&
              allocator.Stats().readback_bytes == 0 &&
              allocator.Stats().live_bytes == probe.frame.AllocationBytes(),
          "transient bridge resources leaked");
    probe.frame.Reset();
    Check(allocator.Stats().live_resources == 0 && allocator.Stats().live_bytes == 0,
          "frame release did not return its budget");
    ComPtr<ID3D12InfoQueue> queue;
    if (SUCCEEDED(context.Device()->QueryInterface(IID_PPV_ARGS(queue.ReleaseAndGetAddressOf())))) {
      for (UINT64 index = 0; index < queue->GetNumStoredMessages(); ++index) {
        SIZE_T size{};
        winrt::check_hresult(queue->GetMessage(index, nullptr, &size));
        std::vector<std::uint8_t> storage(size);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        winrt::check_hresult(queue->GetMessage(index, message, &size));
        Check(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                  message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION, message->pDescription);
      }
    }
    std::cout << "VideoCore GpuCommandSink DMA, boundaries, tickets and frame sampling passed\n";
    return 0;
  } catch (const winrt::hresult_error &error) {
    std::cerr << "HRESULT: " << static_cast<unsigned long>(error.code().value) << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
