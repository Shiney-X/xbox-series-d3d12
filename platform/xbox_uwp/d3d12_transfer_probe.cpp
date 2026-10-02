// SPDX-License-Identifier: GPL-2.0-or-later
#include "d3d12_transfer_probe.h"
#include "d3d12_pipeline_cache.h"

#include <winrt/base.h>

#include <array>
#include <cstring>
#include <initializer_list>

namespace {
bool CheckPixels(ID3D12Resource *readback,
                 const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint,
                 const std::array<std::uint8_t, 4> &expected) {
  void *mapped{};
  const D3D12_RANGE range{static_cast<SIZE_T>(footprint.Offset),
                        static_cast<SIZE_T>(readback->GetDesc().Width)};
  winrt::check_hresult(readback->Map(0, &range, &mapped));
  bool passed = true;
  for (UINT y = 0; y < footprint.Footprint.Height; ++y) {
    for (UINT x = 0; x < footprint.Footprint.Width; ++x) {
      const auto *pixel = static_cast<const std::uint8_t *>(mapped) + footprint.Offset +
                          y * footprint.Footprint.RowPitch + x * 4;
      passed = passed && std::memcmp(pixel, expected.data(), expected.size()) == 0;
    }
  }
  const D3D12_RANGE no_write{0, 0};
  readback->Unmap(0, &no_write);
  return passed;
}

D3D12Resource MakeBuffer(D3D12ResourceAllocator &allocator, UINT64 bytes,
                         D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state) {
  D3D12Resource result;
  winrt::check_hresult(allocator.CreateBuffer(bytes, heap, state, result));
  return result;
}
} // namespace

D3D12TransferProbeResult RunD3D12TransferProbe(D3D12DeviceContext &context,
                                               D3D12ResourceAllocator &allocator,
                                               D3D12TransferStats &stats) {
  D3D12TransferProbeResult result;
  constexpr std::array<UINT, 4> Expected{17, 42, 99, 1234};
  auto upload = MakeBuffer(allocator, 64, D3D12_HEAP_TYPE_UPLOAD,
                           D3D12_RESOURCE_STATE_GENERIC_READ);
  auto storage = MakeBuffer(allocator, 64, D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_STATE_COMMON);
  auto buffer_readback = MakeBuffer(allocator, 64, D3D12_HEAP_TYPE_READBACK,
                                    D3D12_RESOURCE_STATE_COPY_DEST);
  void *mapped{};
  const D3D12_RANGE no_access{0, 0};
  winrt::check_hresult(upload->Map(0, &no_access, &mapped));
  std::memcpy(static_cast<std::uint8_t *>(mapped) + 8, Expected.data(), sizeof(Expected));
  const D3D12_RANGE written{8, 8 + sizeof(Expected)};
  upload->Unmap(0, &written);

  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 7;
  desc.Height = 3;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12Resource color;
  winrt::check_hresult(allocator.CreateTexture2D(desc, D3D12_RESOURCE_STATE_COMMON, color));
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  context.Device()->GetCopyableFootprints(&desc, 0, 1, 512, &footprint,
                                          nullptr, nullptr, nullptr);
  // Explicitly include the footprint offset; do not depend on total-size semantics.
  const UINT64 placed_size = footprint.Offset +
      static_cast<UINT64>(footprint.Footprint.RowPitch) * desc.Height;
  auto texture_upload = MakeBuffer(allocator, placed_size, D3D12_HEAP_TYPE_UPLOAD,
                                   D3D12_RESOURCE_STATE_GENERIC_READ);
  auto texture_readback = MakeBuffer(allocator, placed_size, D3D12_HEAP_TYPE_READBACK,
                                     D3D12_RESOURCE_STATE_COPY_DEST);
  auto clear_readback = MakeBuffer(allocator, placed_size, D3D12_HEAP_TYPE_READBACK,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
  auto resolve_readback = MakeBuffer(allocator, placed_size, D3D12_HEAP_TYPE_READBACK,
                                     D3D12_RESOURCE_STATE_COPY_DEST);
  winrt::check_hresult(texture_upload->Map(0, &no_access, &mapped));
  constexpr std::array<std::uint8_t, 4> Uploaded{0, 0, 255, 255};
  for (UINT y = 0; y < desc.Height; ++y) {
    for (UINT x = 0; x < footprint.Footprint.Width; ++x) {
      auto *address = static_cast<std::uint8_t *>(mapped) + footprint.Offset +
                      y * footprint.Footprint.RowPitch + x * 4;
      std::memcpy(address, Uploaded.data(), Uploaded.size());
    }
  }
  const D3D12_RANGE texture_written{static_cast<SIZE_T>(footprint.Offset),
                                   static_cast<SIZE_T>(placed_size)};
  texture_upload->Unmap(0, &texture_written);

  D3D12DescriptorArena rtvs;
  rtvs.Initialize(context.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
  (void)rtvs.Allocate(2);
  context.Device()->CreateRenderTargetView(color.Get(), nullptr, rtvs.Cpu(0));
  D3D12_RESOURCE_DESC msaa_desc = desc;
  D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support{desc.Format, {}, {}};
  winrt::check_hresult(context.Device()->CheckFeatureSupport(
      D3D12_FEATURE_FORMAT_SUPPORT, &format_support, sizeof(format_support)));
  if ((format_support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE) != 0) {
    for (const UINT samples : {4U, 2U}) {
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality{};
      quality.Format = desc.Format;
      quality.SampleCount = samples;
      winrt::check_hresult(context.Device()->CheckFeatureSupport(
          D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality, sizeof(quality)));
      if (quality.NumQualityLevels != 0) {
        result.resolve_supported = true;
        result.sample_count = samples;
        msaa_desc.SampleDesc.Count = samples;
        break;
      }
    }
  }
  D3D12Resource msaa;
  if (result.resolve_supported) {
    winrt::check_hresult(allocator.CreateTexture2D(msaa_desc, D3D12_RESOURCE_STATE_COMMON, msaa));
    context.Device()->CreateRenderTargetView(msaa.Get(), nullptr, rtvs.Cpu(1));
  }

  D3D12_RESOURCE_DESC depth_desc = desc;
  depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
  depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12Resource depth;
  winrt::check_hresult(allocator.CreateTexture2D(depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, depth));
  D3D12DescriptorArena dsv;
  dsv.Initialize(context.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  (void)dsv.Allocate();
  context.Device()->CreateDepthStencilView(depth.Get(), nullptr, dsv.Cpu(0));
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT depth_footprint{};
  UINT64 depth_bytes{};
  context.Device()->GetCopyableFootprints(&depth_desc, 0, 1, 0, &depth_footprint,
                                          nullptr, nullptr, &depth_bytes);
  auto depth_readback = MakeBuffer(allocator, depth_bytes, D3D12_HEAP_TYPE_READBACK,
                                   D3D12_RESOURCE_STATE_COPY_DEST);

  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commands_allocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
  context.CreateDirectCommands(commands_allocator, commands);
  D3D12CommandEncoder encoder(commands.Get(), stats);
  encoder.Track(upload.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
  encoder.Track(storage.Get(), D3D12_RESOURCE_STATE_COMMON);
  encoder.Track(buffer_readback.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
  encoder.CopyBuffer(storage.Get(), 12, upload.Get(), 8, sizeof(Expected));
  encoder.CopyBuffer(buffer_readback.Get(), 4, storage.Get(), 12, sizeof(Expected));
  encoder.Track(color.Get(), D3D12_RESOURCE_STATE_COMMON);
  encoder.Track(texture_upload.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
  for (ID3D12Resource *resource : {texture_readback.Get(), clear_readback.Get(),
                                  resolve_readback.Get(), depth_readback.Get()}) {
    encoder.Track(resource, D3D12_RESOURCE_STATE_COPY_DEST);
  }
  encoder.CopyBufferToTexture(color.Get(), texture_upload.Get(), footprint);
  encoder.CopyTextureToBuffer(texture_readback.Get(), color.Get(), footprint);
  encoder.ClearColor(color.Get(), rtvs.Cpu(0), {1, 0, 0, 1});
  encoder.CopyTextureToBuffer(clear_readback.Get(), color.Get(), footprint);
  if (result.resolve_supported) {
    encoder.Track(msaa.Get(), D3D12_RESOURCE_STATE_COMMON);
    encoder.ClearColor(msaa.Get(), rtvs.Cpu(1), {0, 1, 0, 1});
    encoder.Resolve(color.Get(), msaa.Get());
    encoder.CopyTextureToBuffer(resolve_readback.Get(), color.Get(), footprint);
  }
  encoder.Track(depth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
  encoder.ClearDepth(depth.Get(), dsv.Cpu(0), 0.5F);
  encoder.CopyTextureToBuffer(depth_readback.Get(), depth.Get(), depth_footprint);
  winrt::check_hresult(commands->Close());
  context.Wait(context.Submit(commands.Get()));

  const D3D12_RANGE read_range{4, 4 + sizeof(Expected)};
  winrt::check_hresult(buffer_readback->Map(0, &read_range, &mapped));
  result.passed = std::memcmp(static_cast<const std::uint8_t *>(mapped) + 4,
                              Expected.data(), sizeof(Expected)) == 0;
  buffer_readback->Unmap(0, &no_access);
  result.passed = CheckPixels(texture_readback.Get(), footprint, Uploaded) && result.passed;
  result.passed = CheckPixels(clear_readback.Get(), footprint, {255, 0, 0, 255}) && result.passed;
  if (result.resolve_supported) {
    result.passed = CheckPixels(resolve_readback.Get(), footprint, {0, 255, 0, 255}) && result.passed;
  }
  const D3D12_RANGE depth_range{0, static_cast<SIZE_T>(depth_bytes)};
  winrt::check_hresult(depth_readback->Map(0, &depth_range, &mapped));
  for (UINT y = 0; y < depth_desc.Height; ++y) {
    for (UINT x = 0; x < static_cast<UINT>(depth_desc.Width); ++x) {
      float value{};
      const auto *address = static_cast<const std::uint8_t *>(mapped) + depth_footprint.Offset +
                            y * depth_footprint.Footprint.RowPitch + x * sizeof(float);
      std::memcpy(&value, address, sizeof(value));
      result.passed = result.passed && value == 0.5F;
    }
  }
  depth_readback->Unmap(0, &no_access);
  return result;
}
