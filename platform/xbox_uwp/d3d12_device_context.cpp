// SPDX-License-Identifier: GPL-2.0-or-later

#include "d3d12_device_context.h"

#include <dxgi1_4.h>
#include <winrt/base.h>

using Microsoft::WRL::ComPtr;

D3D12DeviceContext::~D3D12DeviceContext() {
  if (fence_event_ != INVALID_HANDLE_VALUE) {
    CloseHandle(fence_event_);
  }
}

void D3D12DeviceContext::Initialize() {
  winrt::check_hresult(D3D12CreateDevice(
      nullptr, D3D_FEATURE_LEVEL_11_0,
      IID_PPV_ARGS(device_.ReleaseAndGetAddressOf())));

  D3D12_COMMAND_QUEUE_DESC queue_description{};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  winrt::check_hresult(device_->CreateCommandQueue(
      &queue_description,
      IID_PPV_ARGS(direct_queue_.ReleaseAndGetAddressOf())));

  winrt::check_hresult(device_->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.ReleaseAndGetAddressOf())));
  fence_event_ = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  if (fence_event_ == nullptr) {
    fence_event_ = INVALID_HANDLE_VALUE;
    winrt::throw_last_error();
  }
}

void D3D12DeviceContext::CreateDirectCommands(
    ComPtr<ID3D12CommandAllocator> &allocator,
    ComPtr<ID3D12GraphicsCommandList> &list) const {
  winrt::check_hresult(device_->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      IID_PPV_ARGS(allocator.ReleaseAndGetAddressOf())));
  winrt::check_hresult(device_->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
      IID_PPV_ARGS(list.ReleaseAndGetAddressOf())));
}

void D3D12DeviceContext::WaitForGpu() {
  const UINT64 value = ++fence_value_;
  winrt::check_hresult(direct_queue_->Signal(fence_.Get(), value));
  if (fence_->GetCompletedValue() >= value) {
    return;
  }

  winrt::check_hresult(fence_->SetEventOnCompletion(value, fence_event_));
  const DWORD wait_result = WaitForSingleObjectEx(fence_event_, INFINITE, FALSE);
  if (wait_result == WAIT_FAILED) {
    winrt::throw_last_error();
  }
  if (wait_result != WAIT_OBJECT_0) {
    winrt::throw_hresult(E_UNEXPECTED);
  }
}

bool D3D12DeviceContext::TryTrim() {
  ComPtr<IDXGIDevice3> dxgi_device;
  const HRESULT result = device_.As(&dxgi_device);
  if (result == E_NOINTERFACE) {
    return false;
  }
  winrt::check_hresult(result);
  dxgi_device->Trim();
  return true;
}
