// SPDX-License-Identifier: GPL-2.0-or-later
#include "d3d12_graphics_probe.h"
#include <array>
#include <cstdio>
#include <winrt/base.h>

bool RunD3D12GraphicsProbe(D3D12DeviceContext &context,
                           D3D12ResourceAllocator &allocator,
                           D3D12PipelineCache &cache,
                           D3D12_SHADER_BYTECODE vertex,
                           D3D12_SHADER_BYTECODE fragment,
                           D3D12TransferStats *stats) {
  std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
  for (UINT i = 0; i < 2; ++i) {
    parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[i].Constants.ShaderRegister = 0;
    parameters[i].Constants.RegisterSpace = i + 1;
    parameters[i].Constants.Num32BitValues = 30;
    parameters[i].ShaderVisibility =
        i == 0 ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
  }
  D3D12_ROOT_SIGNATURE_DESC desc{};
  desc.NumParameters = 2;
  desc.pParameters = parameters.data();
  const auto root = cache.RootSignature(desc);
  const auto pipeline =
      cache.Graphics(root, vertex, fragment, DXGI_FORMAT_R8G8B8A8_UNORM, false);
  if (cache.RootSignature(desc) != root ||
      cache.Graphics(root, vertex, fragment, DXGI_FORMAT_R8G8B8A8_UNORM, false)
              .Get() != pipeline.Get())
    winrt::throw_hresult(E_UNEXPECTED);
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
  winrt::check_hresult(allocator.CreateTexture2D(
      target_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, target));
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 bytes{};
  context.Device()->GetCopyableFootprints(&target_desc, 0, 1, 0, &footprint,
                                          nullptr, nullptr, &bytes);
  D3D12Resource readback;
  winrt::check_hresult(allocator.CreateBuffer(bytes, D3D12_HEAP_TYPE_READBACK,
                                              D3D12_RESOURCE_STATE_COPY_DEST,
                                              readback));
  D3D12DescriptorArena rtv;
  rtv.Initialize(context.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  const auto handle = rtv.Cpu(rtv.Allocate());
  context.Device()->CreateRenderTargetView(target.Get(), nullptr, handle);
  bool passed = true;
  for (UINT draw = 0; draw < 2; ++draw) {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commands_allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
    context.CreateDirectCommands(commands_allocator, commands);
    D3D12TransferStats local_stats;
    D3D12CommandEncoder encoder(commands.Get(), stats ? *stats : local_stats);
    encoder.Track(target.Get(), draw == 0 ? D3D12_RESOURCE_STATE_RENDER_TARGET
                                          : D3D12_RESOURCE_STATE_COPY_SOURCE);
    encoder.Track(readback.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    encoder.Transition(target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands->SetPipelineState(pipeline.Get());
    commands->SetGraphicsRootSignature(root->Get());
    std::array<UINT, 30> vs{}, ps{};
    // PushData.user_data starts at DWORD 4; preserve the exact 120-byte ABI.
    vs[4] = draw == 0 ? 0x3f800000u : 0u;
    ps[4] = draw == 0 ? 0u : 0x3f800000u;
    commands->SetGraphicsRoot32BitConstants(0, 30, vs.data(), 0);
    commands->SetGraphicsRoot32BitConstants(1, 30, ps.data(), 0);
    const D3D12_VIEWPORT viewport{0, 0, 4, 4, 0, 1};
    const D3D12_RECT scissor{0, 0, 4, 4};
    commands->RSSetViewports(1, &viewport);
    commands->RSSetScissorRects(1, &scissor);
    commands->OMSetRenderTargets(1, &handle, FALSE, nullptr);
    const float clear[]{0, 0, 1, 1};
    commands->ClearRenderTargetView(handle, clear, 0, nullptr);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3, 1, 0, 0);
    encoder.CopyTextureToBuffer(readback.Get(), target.Get(), footprint);
    winrt::check_hresult(commands->Close());
    context.Wait(context.Submit(commands.Get()));
    void *mapped{};
    const D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
    winrt::check_hresult(readback->Map(0, &range, &mapped));
    for (UINT y = 0; y < 4; ++y)
      for (UINT x = 0; x < 4; ++x) {
        const auto *pixel = static_cast<const std::uint8_t *>(mapped) +
                            footprint.Offset +
                            y * footprint.Footprint.RowPitch + x * 4;
        const bool match = pixel[0] == (draw == 0 ? 255 : 0) &&
                           pixel[1] == (draw == 0 ? 0 : 255) && pixel[2] == 0 &&
                           pixel[3] == 255;
        if (!match)
          std::fprintf(stderr,
                       "graphics readback draw=%u (%u,%u): %u,%u,%u,%u\n", draw,
                       x, y, pixel[0], pixel[1], pixel[2], pixel[3]);
        passed = passed && match;
      }
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
  }
  return passed;
}
