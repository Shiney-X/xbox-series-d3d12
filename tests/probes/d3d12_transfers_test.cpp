// SPDX-License-Identifier: GPL-2.0-or-later
#include "d3d12_transfer_probe.h"

#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <winrt/base.h>

#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
void Check(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Action> void Reject(Action action) {
  try {
    action();
  } catch (const winrt::hresult_error &error) {
    Check(error.code().value == E_INVALIDARG, "wrong rejection code");
    return;
  }
  throw std::runtime_error("invalid transfer request accepted");
}

void TestValidation(D3D12DeviceContext &context) {
  D3D12ResourceAllocator allocator;
  allocator.Initialize(context.Device(), 1024ULL * 1024ULL);
  D3D12Resource upload;
  D3D12Resource readback;
  D3D12Resource storage;
  winrt::check_hresult(allocator.CreateBuffer(512, D3D12_HEAP_TYPE_UPLOAD,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, upload));
  winrt::check_hresult(allocator.CreateBuffer(512, D3D12_HEAP_TYPE_READBACK,
                                              D3D12_RESOURCE_STATE_COPY_DEST, readback));
  winrt::check_hresult(allocator.CreateBuffer(64, D3D12_HEAP_TYPE_DEFAULT,
                                              D3D12_RESOURCE_STATE_COMMON, storage));
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 3;
  desc.Height = 2;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12Resource texture;
  winrt::check_hresult(allocator.CreateTexture2D(desc, D3D12_RESOURCE_STATE_COMMON, texture));
  ComPtr<ID3D12CommandAllocator> commands_allocator;
  ComPtr<ID3D12GraphicsCommandList> commands;
  context.CreateDirectCommands(commands_allocator, commands);
  D3D12TransferStats stats;
  D3D12CommandEncoder encoder(commands.Get(), stats);
  Reject([&] { (void)encoder.State(storage.Get()); });
  encoder.Track(storage.Get(), D3D12_RESOURCE_STATE_COMMON);
  encoder.Track(upload.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
  encoder.Track(readback.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
  encoder.Track(texture.Get(), D3D12_RESOURCE_STATE_COMMON);
  Reject([&] { encoder.Track(nullptr, D3D12_RESOURCE_STATE_COMMON); });
  Reject([&] { encoder.Track(storage.Get(), D3D12_RESOURCE_STATE_COPY_DEST); });
  Reject([&] { encoder.Transition(upload.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE); });
  Reject([&] { encoder.Transition(readback.Get(), D3D12_RESOURCE_STATE_COMMON); });
  Reject([&] { encoder.Transition(storage.Get(), D3D12_RESOURCE_STATE_COPY_DEST |
                                                D3D12_RESOURCE_STATE_COPY_SOURCE); });
  Reject([&] { encoder.Transition(storage.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET); });
  Reject([&] { encoder.Transition(texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS); });
  Reject([&] { encoder.Transition(texture.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER); });
  Reject([&] { encoder.UavBarrier(texture.Get()); });
  Reject([&] { encoder.CopyBuffer(storage.Get(), 0, upload.Get(), 0, 0); });
  Reject([&] { encoder.CopyBuffer(storage.Get(), 60, upload.Get(), 0, 8); });
  Reject([&] { encoder.CopyBuffer(storage.Get(), 0, upload.Get(), std::numeric_limits<UINT64>::max(), 8); });
  Reject([&] { encoder.CopyBuffer(storage.Get(), 0, storage.Get(), 0, 8); });
  Reject([&] { encoder.CopyBuffer(storage.Get(), 0, readback.Get(), 0, 8); });
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  context.Device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, nullptr);
  auto bad = footprint;
  bad.Footprint.RowPitch += 256;
  Reject([&] { encoder.CopyBufferToTexture(texture.Get(), upload.Get(), bad); });
  bad = footprint;
  bad.Offset = 1;
  Reject([&] { encoder.CopyTextureToBuffer(readback.Get(), texture.Get(), bad); });
  bad = footprint;
  bad.Offset = 512;
  Reject([&] { encoder.CopyTextureToBuffer(readback.Get(), texture.Get(), bad); });
  Reject([&] { encoder.CopyBufferToTexture(texture.Get(), readback.Get(), footprint); });
  Reject([&] { encoder.CopyTextureToBuffer(upload.Get(), texture.Get(), footprint); });
  Reject([&] { encoder.ClearColor(texture.Get(), {}, {0, 0, 0, 1}); });
  Reject([&] { encoder.ClearDepth(texture.Get(), {}, 0.5F); });
  Reject([&] { encoder.Resolve(texture.Get(), texture.Get()); });
  encoder.Transition(texture.Get(), D3D12_RESOURCE_STATE_COMMON);
  Check(stats.rejected_requests == 23 && stats.transitions == 0 &&
            stats.redundant_transitions == 1 &&
            encoder.State(texture.Get()) == D3D12_RESOURCE_STATE_COMMON &&
            encoder.State(storage.Get()) == D3D12_RESOURCE_STATE_COMMON,
        "rejected request emitted a transition or changed recording state");
  winrt::check_hresult(commands->Close());
  context.Wait(context.Submit(commands.Get()));
}

void TestBufferDecay(D3D12DeviceContext &context) {
  D3D12ResourceAllocator allocator;
  allocator.Initialize(context.Device(), 1024ULL * 1024ULL);
  D3D12Resource upload;
  D3D12Resource storage;
  D3D12Resource readback;
  winrt::check_hresult(allocator.CreateBuffer(64, D3D12_HEAP_TYPE_UPLOAD,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, upload));
  winrt::check_hresult(allocator.CreateBuffer(64, D3D12_HEAP_TYPE_DEFAULT,
                                              D3D12_RESOURCE_STATE_COMMON, storage));
  winrt::check_hresult(allocator.CreateBuffer(64, D3D12_HEAP_TYPE_READBACK,
                                              D3D12_RESOURCE_STATE_COPY_DEST, readback));
  constexpr UINT Expected = 12345;
  void *mapped{};
  const D3D12_RANGE no_read{0, 0};
  winrt::check_hresult(upload->Map(0, &no_read, &mapped));
  std::memcpy(mapped, &Expected, sizeof(Expected));
  const D3D12_RANGE written{0, sizeof(Expected)};
  upload->Unmap(0, &written);
  ComPtr<ID3D12CommandAllocator> commands_allocator;
  ComPtr<ID3D12GraphicsCommandList> commands;
  context.CreateDirectCommands(commands_allocator, commands);
  D3D12TransferStats stats;
  D3D12_RESOURCE_STATES next_state{};
  {
    D3D12CommandEncoder first(commands.Get(), stats);
    first.Track(upload.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
    first.Track(storage.Get(), D3D12_RESOURCE_STATE_COMMON);
    first.CopyBuffer(storage.Get(), 0, upload.Get(), 0, sizeof(Expected));
    Check(first.State(storage.Get()) == D3D12_RESOURCE_STATE_COPY_DEST,
          "recorded copy destination state differs");
    next_state = first.StateAfterExecution(storage.Get());
    Check(next_state == D3D12_RESOURCE_STATE_COMMON &&
              first.StateAfterExecution(upload.Get()) == D3D12_RESOURCE_STATE_GENERIC_READ,
          "buffer decay or immutable upload state differs");
  }
  winrt::check_hresult(commands->Close());
  context.Wait(context.Submit(commands.Get()));
  winrt::check_hresult(commands_allocator->Reset());
  winrt::check_hresult(commands->Reset(commands_allocator.Get(), nullptr));
  {
    D3D12CommandEncoder second(commands.Get(), stats);
    second.Track(storage.Get(), next_state);
    second.Track(readback.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    second.CopyBuffer(readback.Get(), 0, storage.Get(), 0, sizeof(Expected));
  }
  winrt::check_hresult(commands->Close());
  context.Wait(context.Submit(commands.Get()));
  const D3D12_RANGE read_range{0, sizeof(Expected)};
  winrt::check_hresult(readback->Map(0, &read_range, &mapped));
  UINT actual{};
  std::memcpy(&actual, mapped, sizeof(actual));
  readback->Unmap(0, &no_read);
  Check(actual == Expected && stats.buffer_copies == 2 && stats.rejected_requests == 0,
        "buffer copy across ExecuteCommandLists boundaries failed");
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
    TestValidation(context);
    TestBufferDecay(context);
    D3D12ResourceAllocator allocator;
    allocator.Initialize(context.Device(), 16ULL * 1024ULL * 1024ULL);
    D3D12TransferStats stats;
    const auto probe = RunD3D12TransferProbe(context, allocator, stats);
    Check(probe.passed && probe.resolve_supported && probe.sample_count > 1,
          "buffer/texture/color/depth/MSAA readback probe failed");
    Check(stats.buffer_copies == 2 && stats.texture_copies == 5 &&
              stats.color_clears == 2 && stats.depth_clears == 1 && stats.resolves == 1 &&
              stats.rejected_requests == 0,
          "transfer probe counters differ");
    Check(allocator.Stats().live_bytes == 0 && allocator.Stats().live_resources == 0 &&
              allocator.Stats().failed_allocations == 0,
          "transfer probe leaked resources or exceeded budget");
    ComPtr<ID3D12InfoQueue> queue;
    if (SUCCEEDED(context.Device()->QueryInterface(IID_PPV_ARGS(queue.ReleaseAndGetAddressOf())))) {
      for (UINT64 index = 0; index < queue->GetNumStoredMessages(); ++index) {
        SIZE_T size{};
        winrt::check_hresult(queue->GetMessage(index, nullptr, &size));
        std::vector<std::uint8_t> storage(size);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        winrt::check_hresult(queue->GetMessage(index, message, &size));
        Check(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                  message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION,
              message->pDescription);
      }
    }
    std::cout << "D3D12 states, bounds, padded copies, clears and MSAA resolve passed\n";
    return 0;
  } catch (const winrt::hresult_error &error) {
    std::cerr << "HRESULT: " << static_cast<unsigned long>(error.code().value) << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
