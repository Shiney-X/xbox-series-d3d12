// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "d3d12_device_context.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

enum class LibraryScanState : std::uint8_t {
  Inactive,
  Scanning,
  Ready,
  Empty,
  Failed,
};

enum class GameIconState : std::uint8_t {
  Missing,
  Ready,
  Invalid,
};

struct XboxGameListEntry {
  std::string title;
  std::string title_id;
  std::string app_version;
  GameIconState icon_state{GameIconState::Missing};
  std::uint32_t icon_width{};
  std::uint32_t icon_height{};
  std::uint32_t icon_error{};
  std::uint64_t icon_hash{};
  std::vector<std::uint8_t> icon_bgra8;
};

struct XboxShellState {
  XboxShellPage page{XboxShellPage::Home};
  std::uint32_t selected_item{};
  bool core_ready{};
  bool probes_passed{};
  std::string_view upstream_version{"unknown"};
  LibraryFolderState library_folder_state{LibraryFolderState::NotConfigured};
  std::string library_folder_name;
  std::string library_breadcrumb;
  std::vector<std::string> library_entries;
  std::uint32_t selected_library_entry{};
  bool library_at_device_root{true};
  bool library_selection_confirmed{};
  LibraryScanState library_scan_state{LibraryScanState::Inactive};
  std::vector<XboxGameListEntry> games;
  std::uint32_t selected_game{};
};

class D3D12StatusRenderer final {
public:
  ~D3D12StatusRenderer();

  void Initialize(IUnknown *core_window, float width, float height);
  void Render(const XboxShellState &state);
  void Flush();
  [[nodiscard]] bool TryTrim();
  [[nodiscard]] bool SubmissionReady() const noexcept { return initialized_; }
  [[nodiscard]] std::string SubmissionDetails() const;
  [[nodiscard]] bool SelectedGameIconReady() const noexcept {
    return selected_icon_ready_;
  }

private:
  void CreateShellPipeline();
  void DrawRectangle(float x, float y, float width, float height,
                     const std::array<float, 4> &color);
  void DrawGameIcon(float x, float y, float width, float height);
  void DrawText(std::string_view text, float x, float y, float pixel_size,
                const std::array<float, 4> &color);
  void DrawHome(const XboxShellState &state);
  void DrawPage(const XboxShellState &state);
  void EnsureSelectedGameIcon(const XboxShellState &state);
  void ResetGameIcon() noexcept;
  [[nodiscard]] bool UploadGameIcon(const XboxGameListEntry &game);

  static constexpr UINT FrameCount = 2;

  struct FrameContext {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
    UINT64 fence_value{};
  };

  D3D12DeviceContext device_context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> icon_srv_heap_;
  std::array<FrameContext, FrameCount> frames_;
  // Borrowed only while recording a frame; frames_ owns the command lists.
  ID3D12GraphicsCommandList *command_list_{};
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount>
      render_targets_;
  Microsoft::WRL::ComPtr<ID3D12Resource> selected_icon_texture_;
  UINT64 submitted_frames_{};
  UINT64 allocator_reuses_{};
  bool initialized_{};
  std::uint64_t selected_icon_hash_{};
  bool selected_icon_ready_{};
  UINT rtv_descriptor_size_{};
  D3D12_VIEWPORT viewport_{};
  D3D12_RECT scissor_{};
};
