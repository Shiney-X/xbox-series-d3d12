// SPDX-License-Identifier: GPL-2.0-or-later
#include "d3d12_compute_probe.h"
#include <cstdio>

#include <winrt/base.h>

#include <array>
#include <cstring>

bool RunD3D12ComputeProbe(D3D12DeviceContext &context,
                          D3D12ResourceAllocator &allocator,
                          D3D12PipelineCache &cache,
                          D3D12_SHADER_BYTECODE shader,
                          D3D12TransferStats *stats,
                          std::span<const std::uint32_t> push_words,
                          std::uint32_t expected_base) {
  if (!push_words.empty() && push_words.size() != 30) {
    winrt::throw_hresult(E_INVALIDARG);
  }
  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 30;
  D3D12_ROOT_SIGNATURE_DESC desc{};
  desc.NumParameters = push_words.empty() ? 1 : 2;
  desc.pParameters = parameters.data();
  const auto root = cache.RootSignature(desc);
  const auto pipeline = cache.Compute(root, shader);
  if (cache.RootSignature(desc) != root ||
      cache.Compute(root, shader).Get() != pipeline.Get()) {
    winrt::throw_hresult(E_UNEXPECTED);
  }

  D3D12_RESOURCE_DESC texture_desc{};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = 2;
  texture_desc.Height = 2;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = DXGI_FORMAT_R32_UINT;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  D3D12Resource output;
  winrt::check_hresult(allocator.CreateTexture2D(
      texture_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, output));
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 readback_bytes{};
  context.Device()->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint,
                                          nullptr, nullptr, &readback_bytes);
  D3D12Resource readback;
  winrt::check_hresult(
      allocator.CreateBuffer(readback_bytes, D3D12_HEAP_TYPE_READBACK,
                             D3D12_RESOURCE_STATE_COPY_DEST, readback));
  D3D12DescriptorArena descriptors;
  descriptors.Initialize(context.Device(),
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  const UINT slot = descriptors.Allocate();
  D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
  view.Format = texture_desc.Format;
  view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  context.Device()->CreateUnorderedAccessView(output.Get(), nullptr, &view,
                                              descriptors.Cpu(slot));
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commands_allocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
  context.CreateDirectCommands(commands_allocator, commands);
  D3D12TransferStats local_stats;
  D3D12CommandEncoder encoder(commands.Get(), stats ? *stats : local_stats);
  encoder.Track(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  encoder.Track(readback.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
  commands->SetPipelineState(pipeline.Get());
  commands->SetComputeRootSignature(root->Get());
  ID3D12DescriptorHeap *heaps[]{descriptors.Heap()};
  commands->SetDescriptorHeaps(1, heaps);
  commands->SetComputeRootDescriptorTable(0, descriptors.Gpu(slot));
  if (!push_words.empty()) {
    commands->SetComputeRoot32BitConstants(
        1, static_cast<UINT>(push_words.size()), push_words.data(), 0);
  }
  commands->Dispatch(1, 1, 1);
  encoder.UavBarrier(output.Get());
  encoder.CopyTextureToBuffer(readback.Get(), output.Get(), footprint);
  winrt::check_hresult(commands->Close());
  context.Wait(context.Submit(commands.Get()));

  void *mapped{};
  const D3D12_RANGE read_range{0, static_cast<SIZE_T>(readback_bytes)};
  winrt::check_hresult(readback->Map(0, &read_range, &mapped));
  bool passed = true;
  for (UINT y = 0; y < 2; ++y) {
    for (UINT x = 0; x < 2; ++x) {
      UINT value{};
      const auto *address = static_cast<const std::uint8_t *>(mapped) +
                            footprint.Offset +
                            y * footprint.Footprint.RowPitch + x * sizeof(UINT);
      std::memcpy(&value, address, sizeof(value));
      if (value != expected_base + x + 2 * y) {
        std::fprintf(stderr,
                     "compute readback (%u,%u): actual=%u expected=%u\n", x, y,
                     value, expected_base + x + 2 * y);
      }
      passed = passed && value == expected_base + x + 2 * y;
    }
  }
  const D3D12_RANGE no_write{0, 0};
  readback->Unmap(0, &no_write);
  return passed;
}
