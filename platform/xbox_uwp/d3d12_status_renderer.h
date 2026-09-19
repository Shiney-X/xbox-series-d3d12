// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>

class D3D12StatusRenderer final {
public:
    void Initialize(IUnknown* core_window, float width, float height);
    void Render(bool passed);

private:
    static constexpr UINT FrameCount = 2;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> render_targets_;
    UINT rtv_descriptor_size_{};
};
