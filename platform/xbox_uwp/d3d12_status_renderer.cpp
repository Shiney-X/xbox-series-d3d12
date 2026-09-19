// SPDX-License-Identifier: GPL-2.0-or-later

#include "d3d12_status_renderer.h"

#include <winrt/base.h>

#include <algorithm>

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
}

void D3D12StatusRenderer::Render(bool passed) {
    winrt::check_hresult(command_allocator_->Reset());
    winrt::check_hresult(command_list_->Reset(command_allocator_.Get(), nullptr));

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
    constexpr float pass_color[] = {0.02F, 0.42F, 0.12F, 1.0F};
    constexpr float fail_color[] = {0.62F, 0.03F, 0.03F, 1.0F};
    command_list_->ClearRenderTargetView(rtv_handle, passed ? pass_color : fail_color, 0, nullptr);

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
