// SPDX-License-Identifier: GPL-2.0-or-later

#include "d3d12_device_context.h"
#include "d3d12_resource_allocator.h"

#include <dxgi1_4.h>
#include <winrt/base.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT64 TestBudget = 1024ULL * 1024ULL;

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestOwnership(ID3D12Device* device) {
    D3D12ResourceAllocator allocator;
    allocator.Initialize(device, TestBudget);
    D3D12Resource original;
    winrt::check_hresult(allocator.CreateBuffer(
        256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, original));
    const UINT64 allocation_bytes = original.AllocationBytes();
    Check(allocation_bytes >= 256 && allocator.Stats().default_bytes == allocation_bytes,
          "default buffer allocation accounting differs");
    D3D12Resource moved(std::move(original));
    Check(original.Get() == nullptr && original.AllocationBytes() == 0 &&
              allocator.Stats().live_resources == 1,
          "move construction duplicated or lost the allocation");
    D3D12Resource upload;
    winrt::check_hresult(allocator.CreateBuffer(
        256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, upload));
    moved = std::move(upload);
    auto stats = allocator.Stats();
    Check(upload.Get() == nullptr && stats.live_resources == 1 &&
              stats.default_bytes == 0 && stats.upload_bytes == stats.live_bytes &&
              stats.created_resources == 2,
          "move assignment did not release the previous owner");
    ID3D12Resource* preserved = moved.Get();
    Check(allocator.CreateBuffer(0, D3D12_HEAP_TYPE_DEFAULT,
                                  D3D12_RESOURCE_STATE_COPY_DEST, moved) == E_INVALIDARG,
          "zero-sized allocation accepted");
    Check(allocator.CreateBuffer(TestBudget * 2U, D3D12_HEAP_TYPE_DEFAULT,
                                  D3D12_RESOURCE_STATE_COPY_DEST, moved) == E_OUTOFMEMORY,
          "host budget not enforced");
    Check(moved.Get() == preserved && allocator.Stats().live_bytes == stats.live_bytes &&
              allocator.Stats().failed_allocations == 2,
          "failed allocation changed an existing owner or accounting");
    moved.Reset();
    stats = allocator.Stats();
    Check(stats.live_bytes == 0 && stats.live_resources == 0 && stats.upload_bytes == 0,
          "resource reset did not return the budget");

    D3D12Resource survivor;
    {
        D3D12ResourceAllocator temporary;
        temporary.Initialize(device, TestBudget);
        winrt::check_hresult(temporary.CreateBuffer(
            256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, survivor));
    }
    // The shared accounting/device state outlives the allocator object.
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    winrt::check_hresult(survivor->Map(0, &no_read, &mapped));
    survivor->Unmap(0, &no_read);
    survivor.Reset();
}

void TestTextureReadback(D3D12DeviceContext& context) {
    D3D12ResourceAllocator allocator;
    allocator.Initialize(context.Device(), TestBudget);
    constexpr UINT Width = 63;
    constexpr UINT Height = 17;
    constexpr std::size_t SourcePitch = Width * 4U;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = Width;
    description.Height = Height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12Resource texture;
    winrt::check_hresult(allocator.CreateTexture2D(
        description, D3D12_RESOURCE_STATE_COPY_DEST, texture));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT row_count = 0;
    UINT64 row_size = 0;
    UINT64 copy_bytes = 0;
    context.Device()->GetCopyableFootprints(&description, 0, 1, 0, &footprint,
                                            &row_count, &row_size, &copy_bytes);
    Check(row_count == Height && row_size == SourcePitch &&
              footprint.Footprint.RowPitch > SourcePitch,
          "test did not exercise padded texture rows");
    D3D12Resource staging;
    D3D12Resource readback;
    winrt::check_hresult(allocator.CreateBuffer(
        copy_bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, staging));
    winrt::check_hresult(allocator.CreateBuffer(
        copy_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, readback));

    std::vector<std::uint8_t> expected(SourcePitch * Height);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<std::uint8_t>((i * 37U + 11U) & 0xFFU);
    }
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    winrt::check_hresult(staging->Map(0, &no_read, &mapped));
    auto* pixels = static_cast<std::uint8_t*>(mapped);
    for (UINT row = 0; row < Height; ++row) {
        std::memcpy(pixels + footprint.Offset +
                        static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                    expected.data() + static_cast<std::size_t>(row) * SourcePitch, SourcePitch);
    }
    const D3D12_RANGE copy_range{0, static_cast<SIZE_T>(copy_bytes)};
    staging->Unmap(0, &copy_range);

    ComPtr<ID3D12CommandAllocator> command_allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    context.CreateDirectCommands(command_allocator, commands);
    D3D12_TEXTURE_COPY_LOCATION texture_location{};
    texture_location.pResource = texture.Get();
    texture_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION buffer_location{};
    buffer_location.pResource = staging.Get();
    buffer_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    buffer_location.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&texture_location, 0, 0, 0, &buffer_location, nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &barrier);
    buffer_location.pResource = readback.Get();
    commands->CopyTextureRegion(&buffer_location, 0, 0, 0, &texture_location, nullptr);
    winrt::check_hresult(commands->Close());
    const UINT64 ticket = context.Submit(commands.Get());
    context.Wait(ticket);
    winrt::check_hresult(readback->Map(0, &copy_range, &mapped));
    pixels = static_cast<std::uint8_t*>(mapped);
    bool matches = true;
    for (UINT row = 0; row < Height; ++row) {
        matches = matches &&
                  std::memcmp(pixels + footprint.Offset +
                                  static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                              expected.data() + static_cast<std::size_t>(row) * SourcePitch,
                              SourcePitch) == 0;
    }
    readback->Unmap(0, &no_read);
    Check(matches, "BGRA texture readback differs from uploaded pixels");
    const auto stats = allocator.Stats();
    Check(stats.live_resources == 3 && stats.failed_allocations == 0 &&
              stats.live_bytes == stats.default_bytes + stats.upload_bytes + stats.readback_bytes &&
              stats.peak_bytes <= stats.budget_bytes,
          "texture/upload/readback accounting inconsistent");
    readback.Reset();
    staging.Reset();
    texture.Reset();
    Check(allocator.Stats().live_bytes == 0 && allocator.Stats().live_resources == 0,
          "texture test leaked allocation budget");
}

} // namespace

int main() {
    try {
        ComPtr<IDXGIFactory4> factory;
        winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())));
        ComPtr<IDXGIAdapter> warp;
        winrt::check_hresult(factory->EnumWarpAdapter(IID_PPV_ARGS(warp.ReleaseAndGetAddressOf())));
        D3D12DeviceContext context;
        context.Initialize(warp.Get());
        TestOwnership(context.Device());
        TestTextureReadback(context);
        std::cout << "D3D12 WARP: resource budget, moves, release and BGRA texture readback passed\n";
        return 0;
    } catch (const winrt::hresult_error& error) {
        std::cerr << "D3D12 resources failed: " << std::hex
                  << static_cast<std::uint32_t>(error.code().value) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
