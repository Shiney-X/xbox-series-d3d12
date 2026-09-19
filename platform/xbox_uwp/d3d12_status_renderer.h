// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

enum class XboxShellPage : std::uint8_t {
  Home,
  Games,
  Settings,
  Diagnostics,
};

enum class LibraryFolderState : std::uint8_t {
  NotConfigured,
  Restoring,
  Ready,
  Failed,
};

struct XboxShellState {
  XboxShellPage page{XboxShellPage::Home};
  std::uint32_t selected_item{};
  bool core_ready{};
  bool probes_passed{};
  std::string_view upstream_version{"unknown"};
  LibraryFolderState library_folder_state{LibraryFolderState::NotConfigured};
  std::string library_folder_name;
};

class D3D12StatusRenderer final {
public:
  ~D3D12StatusRenderer();

  void Initialize(IUnknown *core_window, float width, float height);
  void Render(const XboxShellState &state);
  [[nodiscard]] bool TryTrim();

private:
  void CreateShellPipeline();
  void DrawRectangle(float x, float y, float width, float height,
                     const std::array<float, 4> &color);
  void DrawText(std::string_view text, float x, float y, float pixel_size,
                const std::array<float, 4> &color);
  void DrawHome(const XboxShellState &state);
  void DrawPage(const XboxShellState &state);
  void WaitForGpu();

  static constexpr UINT FrameCount = 2;

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount>
      render_targets_;
  HANDLE fence_event_{INVALID_HANDLE_VALUE};
  UINT64 fence_value_{};
  UINT rtv_descriptor_size_{};
  D3D12_VIEWPORT viewport_{};
  D3D12_RECT scissor_{};
};
