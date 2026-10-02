// SPDX-License-Identifier: GPL-2.0-or-later

#include "d3d12_device_context.h"

#include <dxgi1_4.h>
#include <winrt/base.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace {

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, D3D12_HEAP_TYPE heap_type,
                                  D3D12_RESOURCE_STATES state, UINT64 size) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heap_type;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> buffer;
    winrt::check_hresult(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &description, state, nullptr,
        IID_PPV_ARGS(buffer.ReleaseAndGetAddressOf())));
    return buffer;
}

int Run() {
    ComPtr<IDXGIFactory4> factory;
    winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())));
    ComPtr<IDXGIAdapter> warp;
    winrt::check_hresult(factory->EnumWarpAdapter(IID_PPV_ARGS(warp.ReleaseAndGetAddressOf())));

    D3D12DeviceContext context;
    context.Initialize(warp.Get());
    context.Wait(0);
    // Reject an unsignaled value instead of waiting for it forever.
    try {
        context.Wait(1);
        std::cerr << "accepted an unsignaled fence ticket\n";
        return 1;
    } catch (const winrt::hresult_error& error) {
        if (error.code().value != E_INVALIDARG) {
            throw;
        }
    }

    constexpr std::array<std::uint32_t, 8> expected{
        0x13579BDFU, 0x2468ACE0U, 0xDEADBEEFU, 0x01234567U,
        0x89ABCDEFU, 0x76543210U, 0x0F0F0F0FU, 0xF0F0F0F0U};
    auto upload = CreateBuffer(context.Device(), D3D12_HEAP_TYPE_UPLOAD,
                               D3D12_RESOURCE_STATE_GENERIC_READ, sizeof(expected));
    auto readback = CreateBuffer(context.Device(), D3D12_HEAP_TYPE_READBACK,
                                 D3D12_RESOURCE_STATE_COPY_DEST, sizeof(expected));
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    winrt::check_hresult(upload->Map(0, &no_read, &mapped));
    std::memcpy(mapped, expected.data(), sizeof(expected));
    const D3D12_RANGE written{0, sizeof(expected)};
    upload->Unmap(0, &written);

    struct Slot {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        UINT64 ticket{};
    };
    std::array<Slot, 2> slots;
    for (auto& slot : slots) {
        context.CreateDirectCommands(slot.allocator, slot.commands);
        winrt::check_hresult(slot.commands->Close());
    }

    UINT64 previous_ticket = 0;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        Slot& slot = slots[index % slots.size()];
        context.Wait(slot.ticket);
        winrt::check_hresult(slot.allocator->Reset());
        winrt::check_hresult(slot.commands->Reset(slot.allocator.Get(), nullptr));
        const UINT64 offset = static_cast<UINT64>(index) * sizeof(std::uint32_t);
        slot.commands->CopyBufferRegion(readback.Get(), offset, upload.Get(), offset,
                                        sizeof(std::uint32_t));
        winrt::check_hresult(slot.commands->Close());
        slot.ticket = context.Submit(slot.commands.Get());
        if (slot.ticket <= previous_ticket) {
            std::cerr << "fence tickets are not monotonic\n";
            return 1;
        }
        previous_ticket = slot.ticket;
    }
    context.WaitForGpu();
    if (context.CompletedValue() < previous_ticket ||
        context.SubmittedLists() != expected.size()) {
        std::cerr << "queue did not complete all submissions\n";
        return 1;
    }

    const D3D12_RANGE read{0, sizeof(expected)};
    winrt::check_hresult(readback->Map(0, &read, &mapped));
    std::array<std::uint32_t, expected.size()> actual{};
    std::memcpy(actual.data(), mapped, sizeof(actual));
    readback->Unmap(0, &no_read);
    if (actual != expected) {
        std::cerr << "GPU readback differs after allocator reuse\n";
        return 1;
    }
    std::cout << "D3D12 WARP: 8 copies, 2 frame contexts, monotonic tickets and readback passed\n";
    return 0;
}

} // namespace

int main() {
    try {
        return Run();
    } catch (const winrt::hresult_error& error) {
        std::cerr << "D3D12 submission failed: " << std::hex
                  << static_cast<std::uint32_t>(error.code().value) << '\n';
        return 1;
    }
}
