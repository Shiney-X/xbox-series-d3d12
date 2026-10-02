// SPDX-License-Identifier: GPL-2.0-or-later
#include "d3d12_videocore_probe.h"

#include <winrt/base.h>

#include <cstring>
#include <vector>

D3D12VideoCoreProbeResult RunD3D12VideoCoreProbe(D3D12DeviceContext &context,
                                                 D3D12ResourceAllocator &allocator,
                                                 D3D12TransferStats &transfers) {
  constexpr UINT Width = 64;
  constexpr UINT Height = 32;
  constexpr UINT Pitch = Width * 4;
  constexpr UINT Bytes = Pitch * Height;
  constexpr VAddr SourceAddress = 0x100000;
  constexpr VAddr DestinationAddress = 0x200000;
  constexpr u32 Green = 0xFF30D070;
  constexpr u32 Orange = 0xFFFFA030;
  D3D12VideoCoreProbeResult result;
  D3D12Resource source;
  D3D12Resource destination;
  winrt::check_hresult(allocator.CreateBuffer(Bytes, D3D12_HEAP_TYPE_DEFAULT,
                                               D3D12_RESOURCE_STATE_COMMON, source));
  winrt::check_hresult(allocator.CreateBuffer(Bytes, D3D12_HEAP_TYPE_DEFAULT,
                                               D3D12_RESOURCE_STATE_COMMON, destination));
  D3D12VideoCoreBridge bridge(context, allocator, transfers);
  bridge.RegisterBuffer(SourceAddress, source.Get());
  bridge.RegisterBuffer(DestinationAddress, destination.Get());
  VideoCore::GpuCommandSink &sink = bridge;
  sink.ScopeMarkerBegin("VideoCore DMA synthetic frame");
  sink.FillBuffer(SourceAddress, Bytes, Green, false);
  sink.FillBuffer(SourceAddress + Bytes / 2, Bytes / 2, Orange, false);
  sink.CopyBuffer(DestinationAddress, SourceAddress, Bytes, false, false);
  sink.ScopedMarkerInsertColor("DMA copied", Green);
  sink.ScopeMarkerEnd();
  sink.OnSubmit();
  result.dma_ticket = sink.Flush();
  sink.CpSync();
  std::vector<std::uint8_t> buffer(Bytes);
  bridge.DownloadBuffer(DestinationAddress, buffer);
  result.buffer_verified = true;
  for (UINT offset = 0; offset < Bytes; offset += static_cast<UINT>(sizeof(u32))) {
    u32 value{};
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    result.buffer_verified = result.buffer_verified && value == (offset < Bytes / 2 ? Green : Orange);
  }
  bridge.CreateLinearFrame(DestinationAddress, Width, Height, Pitch, result.frame);
  sink.OnSubmit();
  result.frame_ticket = sink.Flush();
  sink.Finish();
  result.stats = bridge.Stats();

  const auto frame_desc = result.frame->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 readback_bytes{};
  context.Device()->GetCopyableFootprints(&frame_desc, 0, 1, 0, &footprint,
                                          nullptr, nullptr, &readback_bytes);
  D3D12Resource readback;
  winrt::check_hresult(allocator.CreateBuffer(readback_bytes, D3D12_HEAP_TYPE_READBACK,
                                               D3D12_RESOURCE_STATE_COPY_DEST, readback));
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commands_allocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
  context.CreateDirectCommands(commands_allocator, commands);
  D3D12CommandEncoder encoder(commands.Get(), transfers);
  encoder.Track(result.frame.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  encoder.Track(readback.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
  encoder.CopyTextureToBuffer(readback.Get(), result.frame.Get(), footprint);
  encoder.Transition(result.frame.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  winrt::check_hresult(commands->Close());
  context.Wait(context.Submit(commands.Get()));
  void *mapped{};
  const D3D12_RANGE range{0, static_cast<SIZE_T>(readback_bytes)};
  winrt::check_hresult(readback->Map(0, &range, &mapped));
  result.texture_verified = true;
  for (UINT y = 0; y < Height; ++y) {
    const auto *row = static_cast<const std::uint8_t *>(mapped) + footprint.Offset +
                      y * footprint.Footprint.RowPitch;
    for (UINT x = 0; x < Width; ++x) {
      u32 value{};
      std::memcpy(&value, row + x * sizeof(u32), sizeof(value));
      result.texture_verified = result.texture_verified && value == (y < Height / 2 ? Green : Orange);
    }
  }
  const D3D12_RANGE no_write{0, 0};
  readback->Unmap(0, &no_write);
  return result;
}
