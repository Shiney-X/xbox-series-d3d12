// SPDX-License-Identifier: GPL-2.0-or-later

#include "d3d12_status_renderer.h"
#include "d3d12_compute_probe.h"

#include <dxcapi.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <utility>

namespace {

constexpr char ShellShader[] = R"(
cbuffer DrawConstants : register(b0) {
    float4 rect;
    float4 draw_color;
    float4 draw_options;
};

Texture2D<float4> game_icon : register(t0);
SamplerState icon_sampler : register(s0);

struct VertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

VertexOutput VSMain(uint vertex_id : SV_VertexID) {
    const float2 corners[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
        float2(0.0, 1.0), float2(1.0, 0.0), float2(1.0, 1.0)
    };

    float2 position = rect.xy + corners[vertex_id] * rect.zw;
    VertexOutput output;
    output.position = float4(position.x * 2.0 - 1.0,
                             1.0 - position.y * 2.0, 0.0, 1.0);
    output.color = draw_color;
    output.uv = corners[vertex_id];
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target {
    if (draw_options.x > 0.5) {
        return game_icon.Sample(icon_sampler, input.uv) * input.color;
    }
    return input.color;
}
)";

constexpr std::array<float, 4> Accent{0.10F, 0.85F, 0.48F, 1.0F};
constexpr std::array<float, 4> Focus{0.13F, 0.63F, 1.0F, 1.0F};
constexpr std::array<float, 4> Panel{0.045F, 0.075F, 0.13F, 1.0F};
constexpr std::array<float, 4> PanelSelected{0.07F, 0.13F, 0.22F, 1.0F};
constexpr std::array<float, 4> PrimaryText{0.92F, 0.96F, 1.0F, 1.0F};
constexpr std::array<float, 4> SecondaryText{0.48F, 0.59F, 0.70F, 1.0F};
constexpr std::array<float, 4> Failure{0.95F, 0.20F, 0.20F, 1.0F};

std::array<std::uint8_t, 7> Glyph(char character) {
  switch (character) {
  case 'A':
    return {14, 17, 17, 31, 17, 17, 17};
  case 'B':
    return {30, 17, 17, 30, 17, 17, 30};
  case 'C':
    return {14, 17, 16, 16, 16, 17, 14};
  case 'D':
    return {30, 17, 17, 17, 17, 17, 30};
  case 'E':
    return {31, 16, 16, 30, 16, 16, 31};
  case 'F':
    return {31, 16, 16, 30, 16, 16, 16};
  case 'G':
    return {14, 17, 16, 23, 17, 17, 14};
  case 'H':
    return {17, 17, 17, 31, 17, 17, 17};
  case 'I':
    return {31, 4, 4, 4, 4, 4, 31};
  case 'J':
    return {7, 2, 2, 2, 18, 18, 12};
  case 'K':
    return {17, 18, 20, 24, 20, 18, 17};
  case 'L':
    return {16, 16, 16, 16, 16, 16, 31};
  case 'M':
    return {17, 27, 21, 21, 17, 17, 17};
  case 'N':
    return {17, 25, 21, 19, 17, 17, 17};
  case 'O':
    return {14, 17, 17, 17, 17, 17, 14};
  case 'P':
    return {30, 17, 17, 30, 16, 16, 16};
  case 'Q':
    return {14, 17, 17, 17, 21, 18, 13};
  case 'R':
    return {30, 17, 17, 30, 20, 18, 17};
  case 'S':
    return {15, 16, 16, 14, 1, 1, 30};
  case 'T':
    return {31, 4, 4, 4, 4, 4, 4};
  case 'U':
    return {17, 17, 17, 17, 17, 17, 14};
  case 'V':
    return {17, 17, 17, 17, 17, 10, 4};
  case 'W':
    return {17, 17, 17, 21, 21, 21, 10};
  case 'X':
    return {17, 17, 10, 4, 10, 17, 17};
  case 'Y':
    return {17, 17, 10, 4, 4, 4, 4};
  case 'Z':
    return {31, 1, 2, 4, 8, 16, 31};
  case '0':
    return {14, 17, 19, 21, 25, 17, 14};
  case '1':
    return {4, 12, 4, 4, 4, 4, 14};
  case '2':
    return {14, 17, 1, 2, 4, 8, 31};
  case '3':
    return {30, 1, 1, 14, 1, 1, 30};
  case '4':
    return {2, 6, 10, 18, 31, 2, 2};
  case '5':
    return {31, 16, 16, 30, 1, 1, 30};
  case '6':
    return {14, 16, 16, 30, 17, 17, 14};
  case '7':
    return {31, 1, 2, 4, 8, 8, 8};
  case '8':
    return {14, 17, 17, 14, 17, 17, 14};
  case '9':
    return {14, 17, 17, 15, 1, 1, 14};
  case '-':
    return {0, 0, 0, 31, 0, 0, 0};
  case '.':
    return {0, 0, 0, 0, 0, 12, 12};
  case ':':
    return {0, 12, 12, 0, 12, 12, 0};
  case '/':
    return {1, 1, 2, 4, 8, 16, 16};
  default:
    return {};
  }
}

} // namespace

using Microsoft::WRL::ComPtr;

D3D12StatusRenderer::~D3D12StatusRenderer() {
  if (initialized_) {
    try {
      Flush();
    } catch (...) {
      // Device removal must not throw from a destructor during shutdown.
    }
  }
}

void D3D12StatusRenderer::Flush() {
  if (!initialized_) {
    winrt::throw_hresult(E_UNEXPECTED);
  }
  device_context_.WaitForGpu();
}

bool D3D12StatusRenderer::TryTrim() {
  Flush();
  return device_context_.TryTrim();
}

std::string D3D12StatusRenderer::SubmissionDetails() const {
  return "frame_contexts=" + std::to_string(FrameCount) +
         ";submitted_frames=" + std::to_string(submitted_frames_) +
         ";submitted_lists=" + std::to_string(device_context_.SubmittedLists()) +
         ";allocator_reuses=" + std::to_string(allocator_reuses_) +
         ";last_signaled_ticket=" +
         std::to_string(device_context_.LastSignaledValue()) +
         ";completed_ticket=" + std::to_string(device_context_.CompletedValue()) +
         ";blocking_waits=" + std::to_string(device_context_.BlockingWaits());
}

std::string D3D12StatusRenderer::ResourceDetails() const {
  const auto stats = ResourceStats();
  return "allocation_policy=committed;budget_source=host_cap;"
         "residency_policy=implicit_no_eviction;tracked_scope=owned_buffer_texture;"
         "budget_bytes=" + std::to_string(stats.budget_bytes) +
         ";live_bytes=" + std::to_string(stats.live_bytes) +
         ";peak_bytes=" + std::to_string(stats.peak_bytes) +
         ";default_bytes=" + std::to_string(stats.default_bytes) +
         ";upload_bytes=" + std::to_string(stats.upload_bytes) +
         ";readback_bytes=" + std::to_string(stats.readback_bytes) +
         ";live_resources=" + std::to_string(stats.live_resources) +
         ";created_resources=" + std::to_string(stats.created_resources) +
         ";failed_allocations=" + std::to_string(stats.failed_allocations) +
         ";last_error=" +
         std::to_string(static_cast<std::uint32_t>(stats.last_error));
}

std::string D3D12StatusRenderer::PipelineDetails() const {
  const auto stats = pipeline_cache_.Stats();
  return "cache_scope=device_memory;graphics_scope=host_color_triangle;"
         "compute_probe=dispatch_readback_2x2;compute_passed=" +
         std::to_string(compute_probe_passed_) +
         ";root_creations=" + std::to_string(stats.root_creations) +
         ";graphics_creations=" + std::to_string(stats.graphics_creations) +
         ";compute_creations=" + std::to_string(stats.compute_creations) +
         ";cache_hits=" + std::to_string(stats.cache_hits) +
         ";rtv_slots=" + std::to_string(rtv_heap_.Used()) +
         ";srv_slots=" + std::to_string(icon_srv_heap_.Used());
}

std::string D3D12StatusRenderer::TransferDetails() const {
  return "state_scope=recording;queue=direct;subresources=uniform;"
         "probe=buffer_texture_clear_depth_resolve_readback;probe_passed=" +
         std::to_string(transfer_probe_.passed) +
         ";resolve_supported=" + std::to_string(transfer_probe_.resolve_supported) +
         ";resolve_sample_count=" + std::to_string(transfer_probe_.sample_count) +
         ";transitions=" + std::to_string(transfer_stats_.transitions) +
         ";redundant_transitions=" + std::to_string(transfer_stats_.redundant_transitions) +
         ";uav_barriers=" + std::to_string(transfer_stats_.uav_barriers) +
         ";buffer_copies=" + std::to_string(transfer_stats_.buffer_copies) +
         ";texture_copies=" + std::to_string(transfer_stats_.texture_copies) +
         ";color_clears=" + std::to_string(transfer_stats_.color_clears) +
         ";depth_clears=" + std::to_string(transfer_stats_.depth_clears) +
         ";resolves=" + std::to_string(transfer_stats_.resolves) +
         ";rejected_requests=" + std::to_string(transfer_stats_.rejected_requests);
}

void D3D12StatusRenderer::Initialize(IUnknown *core_window, float width,
                                     float height) {
  device_context_.Initialize();
  resource_allocator_.Initialize(device_context_.Device(), ResourceBudgetBytes);
  pipeline_cache_.Initialize(device_context_.Device());
  transfer_probe_ = RunD3D12TransferProbe(device_context_, resource_allocator_, transfer_stats_);

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
      device_context_.DirectQueue(), core_window, &swap_chain_description,
      nullptr, swap_chain.ReleaseAndGetAddressOf()));
  winrt::check_hresult(swap_chain.As(&swap_chain_));

  viewport_.Width = static_cast<float>(swap_chain_description.Width);
  viewport_.Height = static_cast<float>(swap_chain_description.Height);
  viewport_.MaxDepth = 1.0F;
  scissor_.right = static_cast<LONG>(swap_chain_description.Width);
  scissor_.bottom = static_cast<LONG>(swap_chain_description.Height);

  rtv_heap_.Initialize(device_context_.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                       FrameCount, false);
  (void)rtv_heap_.Allocate(FrameCount);
  icon_srv_heap_.Initialize(device_context_.Device(),
                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  (void)icon_srv_heap_.Allocate();
  ResetGameIcon();

  for (UINT index = 0; index < FrameCount; ++index) {
    winrt::check_hresult(swap_chain_->GetBuffer(
        index, IID_PPV_ARGS(render_targets_[index].ReleaseAndGetAddressOf())));
    device_context_.Device()->CreateRenderTargetView(
        render_targets_[index].Get(), nullptr, rtv_heap_.Cpu(index));
  }

  for (FrameContext &frame : frames_) {
    device_context_.CreateDirectCommands(frame.allocator, frame.commands);
    winrt::check_hresult(frame.commands->Close());
  }

  CreateShellPipeline();
  initialized_ = true;
}

void D3D12StatusRenderer::Render(const XboxShellState &state) {
  if (!initialized_) {
    winrt::throw_hresult(E_UNEXPECTED);
  }
  EnsureSelectedGameIcon(state);
  const UINT frame_index = swap_chain_->GetCurrentBackBufferIndex();
  FrameContext &frame = frames_[frame_index];
  device_context_.Wait(frame.fence_value);
  winrt::check_hresult(frame.allocator->Reset());
  winrt::check_hresult(
      frame.commands->Reset(frame.allocator.Get(), pipeline_state_.Get()));
  command_list_ = frame.commands.Get();
  if (frame.fence_value != 0U) {
    ++allocator_reuses_;
  }

  D3D12CommandEncoder encoder(command_list_, transfer_stats_);
  encoder.Track(render_targets_[frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT);
  auto rtv_handle = rtv_heap_.Cpu(frame_index);
  encoder.ClearColor(render_targets_[frame_index].Get(), rtv_handle,
                     {0.012F, 0.022F, 0.042F, 1.0F});
  command_list_->SetGraphicsRootSignature(root_signature_->Get());
  ID3D12DescriptorHeap *descriptor_heaps[]{icon_srv_heap_.Heap()};
  command_list_->SetDescriptorHeaps(1, descriptor_heaps);
  command_list_->SetGraphicsRootDescriptorTable(
      1, icon_srv_heap_.Gpu(0));
  command_list_->RSSetViewports(1, &viewport_);
  command_list_->RSSetScissorRects(1, &scissor_);
  command_list_->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
  command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  DrawRectangle(0.0F, 0.0F, 1.0F, 0.012F, Accent);
  if (state.page == XboxShellPage::Home) {
    DrawHome(state);
  } else {
    DrawPage(state);
  }

  encoder.Transition(render_targets_[frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT);

  winrt::check_hresult(command_list_->Close());
  frame.fence_value = device_context_.Submit(command_list_);
  winrt::check_hresult(swap_chain_->Present(1, 0));
  ++submitted_frames_;
}

void D3D12StatusRenderer::DrawRectangle(float x, float y, float width,
                                        float height,
                                        const std::array<float, 4> &color) {
  const std::array<float, 12> constants{x,        y,        width,    height,
                                        color[0], color[1], color[2], color[3],
                                        0.0F,     0.0F,     0.0F,     0.0F};
  command_list_->SetGraphicsRoot32BitConstants(
      0, static_cast<UINT>(constants.size()), constants.data(), 0);
  command_list_->DrawInstanced(6, 1, 0, 0);
}

void D3D12StatusRenderer::DrawGameIcon(float x, float y, float width,
                                       float height) {
  const std::array<float, 12> constants{x,    y,    width, height, 1.0F, 1.0F,
                                        1.0F, 1.0F, 1.0F,  0.0F,   0.0F, 0.0F};
  command_list_->SetGraphicsRoot32BitConstants(
      0, static_cast<UINT>(constants.size()), constants.data(), 0);
  command_list_->DrawInstanced(6, 1, 0, 0);
}

void D3D12StatusRenderer::DrawText(std::string_view text, float x, float y,
                                   float pixel_size,
                                   const std::array<float, 4> &color) {
  const float pixel_width = pixel_size / viewport_.Width;
  const float pixel_height = pixel_size / viewport_.Height;
  float cursor = x;
  for (const char raw_character : text) {
    const char character = static_cast<char>(
        std::toupper(static_cast<unsigned char>(raw_character)));
    if (character == ' ') {
      cursor += pixel_width * 4.0F;
      continue;
    }
    const auto rows = Glyph(character);
    for (std::uint32_t row = 0; row < rows.size(); ++row) {
      for (std::uint32_t column = 0; column < 5U; ++column) {
        if ((rows[row] & (1U << (4U - column))) != 0) {
          DrawRectangle(cursor + static_cast<float>(column) * pixel_width,
                        y + static_cast<float>(row) * pixel_height,
                        pixel_width * 0.86F, pixel_height * 0.86F, color);
        }
      }
    }
    cursor += pixel_width * 6.0F;
  }
}

void D3D12StatusRenderer::DrawHome(const XboxShellState &state) {
  DrawText("SHADPS4 XBOX", 0.065F, 0.075F, 8.0F, PrimaryText);
  DrawText("REAL UWP HOST  CORE " + std::string(state.upstream_version), 0.067F,
           0.16F, 4.0F, SecondaryText);

  const auto status_color = state.core_ready ? Accent : Failure;
  DrawRectangle(0.73F, 0.075F, 0.205F, 0.075F, Panel);
  DrawRectangle(0.748F, 0.101F, 0.012F, 0.021F, status_color);
  DrawText(state.core_ready ? "CORE LINKED" : "CORE FAILED", 0.775F, 0.096F,
           4.0F, PrimaryText);

  constexpr std::array<std::string_view, 3> labels{"GAMES", "SETTINGS",
                                                   "DIAGNOSTICS"};
  constexpr std::array<float, 3> positions{0.065F, 0.365F, 0.665F};
  for (std::size_t index = 0; index < labels.size(); ++index) {
    const bool selected = state.selected_item == index;
    if (selected) {
      DrawRectangle(positions[index] - 0.005F, 0.305F, 0.27F, 0.32F, Focus);
    }
    DrawRectangle(positions[index], 0.31F, 0.26F, 0.31F,
                  selected ? PanelSelected : Panel);
    DrawText(labels[index], positions[index] + 0.025F, 0.445F, 5.0F,
             selected ? PrimaryText : SecondaryText);
  }

  DrawText("DPAD MOVE   A SELECT", 0.065F, 0.865F, 4.0F, SecondaryText);
  DrawText(state.probes_passed ? "SYSTEM PROBES PASS" : "SYSTEM PROBES FAIL",
           0.68F, 0.865F, 4.0F, state.probes_passed ? Accent : Failure);
}

void D3D12StatusRenderer::DrawPage(const XboxShellState &state) {
  std::string_view title;
  switch (state.page) {
  case XboxShellPage::Games:
    title = "GAMES";
    break;
  case XboxShellPage::Settings:
    title = "SETTINGS";
    break;
  case XboxShellPage::Diagnostics:
    title = "DIAGNOSTICS";
    break;
  default:
    title = "SHADPS4 XBOX";
    break;
  }

  DrawText(title, 0.065F, 0.075F, 8.0F, PrimaryText);
  DrawRectangle(0.065F, 0.20F, 0.87F, 0.55F, Panel);

  if (state.page == XboxShellPage::Games) {
    switch (state.library_folder_state) {
    case LibraryFolderState::Restoring:
      DrawText("READING USB STORAGE", 0.105F, 0.31F, 5.0F, PrimaryText);
      DrawText("LOADING FOLDERS", 0.105F, 0.43F, 4.0F, SecondaryText);
      break;
    case LibraryFolderState::Ready:
      if (state.library_scan_state == LibraryScanState::Scanning) {
        DrawText("SCANNING GAME LIBRARY", 0.095F, 0.25F, 4.8F, Accent);
        DrawText(state.library_breadcrumb, 0.095F, 0.34F, 3.6F, SecondaryText);
        DrawText("READING PARAM SFO", 0.105F, 0.48F, 4.3F, PrimaryText);
      } else if (state.library_scan_state == LibraryScanState::Empty) {
        DrawText("NO PS4 GAMES FOUND", 0.095F, 0.25F, 4.8F, PrimaryText);
        DrawText(state.library_breadcrumb, 0.095F, 0.34F, 3.6F, SecondaryText);
        DrawText("EXPECTED SCE SYS PARAM SFO", 0.105F, 0.47F, 3.8F,
                 SecondaryText);
        DrawText("AND EBOOT BIN", 0.105F, 0.55F, 3.8F, SecondaryText);
      } else if (state.library_scan_state == LibraryScanState::Failed) {
        DrawText("LIBRARY SCAN FAILED", 0.095F, 0.25F, 4.8F, Failure);
        DrawText("SEE PHASE1-LIBRARY-SCAN JSONL", 0.095F, 0.39F, 3.6F,
                 SecondaryText);
      } else if (state.library_scan_state == LibraryScanState::Ready) {
        DrawText("PS4 GAME LIBRARY", 0.095F, 0.23F, 4.8F, Accent);
        DrawText(state.library_breadcrumb, 0.095F, 0.30F, 3.4F, SecondaryText);
        constexpr std::size_t visible_rows = 4U;
        const std::size_t selected =
            std::min<std::size_t>(state.selected_game, state.games.size() - 1U);
        const std::size_t first =
            selected < visible_rows ? 0U : selected - visible_rows + 1U;
        const std::size_t last =
            std::min(first + visible_rows, state.games.size());
        for (std::size_t index = first; index < last; ++index) {
          const float y = 0.365F + static_cast<float>(index - first) * 0.067F;
          const bool focused = index == selected;
          if (focused) {
            DrawRectangle(0.32F, y - 0.012F, 0.51F, 0.056F, PanelSelected);
            DrawRectangle(0.32F, y - 0.012F, 0.006F, 0.056F, Focus);
          }
          DrawText(state.games[index].title, 0.34F, y, 3.8F,
                   focused ? PrimaryText : SecondaryText);
        }
        DrawRectangle(0.095F, 0.355F, 0.185F, 0.329F, PanelSelected);
        if (selected_icon_ready_) {
          DrawGameIcon(0.10F, 0.364F, 0.175F, 0.311F);
        } else {
          DrawText("NO ICON", 0.125F, 0.50F, 4.0F, SecondaryText);
        }
        DrawText("TITLE ID  " + state.games[selected].title_id, 0.33F, 0.66F,
                 3.4F, PrimaryText);
        DrawText("VERSION  " + state.games[selected].app_version, 0.65F, 0.66F,
                 3.4F, SecondaryText);
      } else {
        DrawText(state.library_selection_confirmed ? "LIBRARY FOLDER SELECTED"
                                                   : "USB FOLDER BROWSER",
                 0.095F, 0.235F, 4.5F,
                 state.library_selection_confirmed ? Accent : PrimaryText);
        DrawText(state.library_breadcrumb, 0.095F, 0.305F, 3.6F, SecondaryText);

        if (state.library_entries.empty()) {
          DrawText("NO SUBFOLDERS", 0.105F, 0.43F, 5.0F, SecondaryText);
          DrawText("X USE THIS FOLDER", 0.105F, 0.55F, 4.0F, Accent);
        } else {
          constexpr std::size_t visible_rows = 5U;
          const std::size_t selected = std::min<std::size_t>(
              state.selected_library_entry, state.library_entries.size() - 1U);
          const std::size_t first =
              selected < visible_rows ? 0U : selected - visible_rows + 1U;
          const std::size_t last =
              std::min(first + visible_rows, state.library_entries.size());
          for (std::size_t index = first; index < last; ++index) {
            const float y = 0.37F + static_cast<float>(index - first) * 0.062F;
            const bool focused = index == selected;
            if (focused) {
              DrawRectangle(0.09F, y - 0.012F, 0.74F, 0.053F, PanelSelected);
              DrawRectangle(0.09F, y - 0.012F, 0.006F, 0.053F, Focus);
            }
            DrawText(state.library_entries[index], 0.11F, y, 3.8F,
                     focused ? PrimaryText : SecondaryText);
          }
          DrawText("A OPEN   X USE CURRENT", 0.64F, 0.70F, 3.0F, Accent);
        }
      }
      break;
    case LibraryFolderState::Failed:
      DrawText("FOLDER ACCESS FAILED", 0.105F, 0.29F, 5.5F, Failure);
      DrawText("SEE PHASE1-LIBRARY.JSONL", 0.105F, 0.43F, 4.0F, SecondaryText);
      DrawText("A TRY AGAIN", 0.105F, 0.56F, 4.0F, PrimaryText);
      break;
    case LibraryFolderState::NotConfigured:
    default:
      DrawText("NO USB STORAGE FOUND", 0.105F, 0.29F, 5.5F, PrimaryText);
      DrawText("A SCAN USB", 0.105F, 0.43F, 5.0F, Accent);
      DrawText("CONNECT A MEDIA USB DEVICE", 0.105F, 0.56F, 3.8F,
               SecondaryText);
      break;
    }
  } else if (state.page == XboxShellPage::Settings) {
    DrawText("APP PROFILE  GAME", 0.105F, 0.31F, 5.0F, PrimaryText);
    DrawText("RENDERER  D3D12", 0.105F, 0.43F, 5.0F, PrimaryText);
    DrawText("UPSTREAM  " + std::string(state.upstream_version), 0.105F, 0.55F,
             5.0F, PrimaryText);
  } else {
    DrawText(state.core_ready ? "CORE LINKED  PASS" : "CORE LINKED  FAIL",
             0.105F, 0.29F, 4.5F, state.core_ready ? Accent : Failure);
    DrawText("UPSTREAM  " + std::string(state.upstream_version), 0.105F, 0.39F,
             4.5F, PrimaryText);
    DrawText("UWP X64  PASS", 0.105F, 0.49F, 4.5F, Accent);
    DrawText("D3D12 DXIL  PASS", 0.105F, 0.59F, 4.5F, Accent);
    DrawText(state.probes_passed ? "SYSTEM PROBES  PASS"
                                 : "SYSTEM PROBES  FAIL",
             0.51F, 0.29F, 4.5F, state.probes_passed ? Accent : Failure);
  }

  DrawText(state.page == XboxShellPage::Games
               ? (state.library_folder_state == LibraryFolderState::Ready
                      ? (state.library_scan_state != LibraryScanState::Inactive
                             ? "DPAD MOVE   B FOLDERS   X RESCAN"
                             : (state.library_at_device_root
                                    ? "DPAD MOVE   A OPEN   B HOME   X SELECT"
                                    : "DPAD MOVE   A OPEN   B UP   X SELECT"))
                      : "A SCAN USB   B BACK")
               : "B BACK",
           0.065F, 0.865F, 4.0F, SecondaryText);
}

void D3D12StatusRenderer::ResetGameIcon() noexcept {
  selected_icon_texture_.Reset();
  selected_icon_hash_ = 0U;
  selected_icon_ready_ = false;
  if (!device_context_.Device() || !icon_srv_heap_.Heap()) {
    return;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC null_view{};
  null_view.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  null_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  null_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  null_view.Texture2D.MipLevels = 1;
  device_context_.Device()->CreateShaderResourceView(
      nullptr, &null_view,
      icon_srv_heap_.Cpu(0));
}

void D3D12StatusRenderer::EnsureSelectedGameIcon(
    const XboxShellState &state) {
  const XboxGameListEntry *desired_icon = nullptr;
  if (state.page == XboxShellPage::Games &&
      state.library_scan_state == LibraryScanState::Ready &&
      !state.games.empty()) {
    const std::size_t selected =
        std::min<std::size_t>(state.selected_game, state.games.size() - 1U);
    const XboxGameListEntry &game = state.games[selected];
    if (game.icon_state == GameIconState::Ready && game.icon_hash != 0U &&
        game.icon_width != 0U && game.icon_height != 0U &&
        game.icon_bgra8.size() ==
            static_cast<std::size_t>(game.icon_width) * game.icon_height * 4U) {
      desired_icon = &game;
    }
  }
  if ((!desired_icon && !selected_icon_ready_) ||
      (desired_icon && selected_icon_ready_ &&
       selected_icon_hash_ == desired_icon->icon_hash)) {
    return;
  }

  // Both frame contexts reference the shared SRV. Drain before overwriting
  // that descriptor or releasing the previous texture. Wait failures must
  // propagate rather than taking the optional-icon fallback path.
  Flush();
  pending_icon_upload_ = {};
  try {
    ResetGameIcon();
    if (desired_icon) {
      selected_icon_ready_ = UploadGameIcon(*desired_icon);
      selected_icon_hash_ = selected_icon_ready_ ? desired_icon->icon_hash : 0U;
    }
  } catch (...) {
    ResetGameIcon();
  }
}

bool D3D12StatusRenderer::UploadGameIcon(const XboxGameListEntry &game) {
  D3D12_RESOURCE_DESC texture_description{};
  texture_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_description.Width = game.icon_width;
  texture_description.Height = game.icon_height;
  texture_description.DepthOrArraySize = 1;
  texture_description.MipLevels = 1;
  texture_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  texture_description.SampleDesc.Count = 1;
  texture_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  D3D12Resource texture;
  winrt::check_hresult(resource_allocator_.CreateTexture2D(
      texture_description, D3D12_RESOURCE_STATE_COPY_DEST, texture));

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 upload_size = 0;
  device_context_.Device()->GetCopyableFootprints(
      &texture_description, 0, 1, 0, &footprint, &row_count, &row_size,
      &upload_size);
  if (row_count != game.icon_height ||
      row_size < static_cast<UINT64>(game.icon_width) * 4U) {
    return false;
  }

  D3D12Resource upload;
  winrt::check_hresult(resource_allocator_.CreateBuffer(
      upload_size, D3D12_HEAP_TYPE_UPLOAD,
      D3D12_RESOURCE_STATE_GENERIC_READ, upload));

  std::uint8_t *mapped = nullptr;
  const D3D12_RANGE no_read{0, 0};
  winrt::check_hresult(
      upload->Map(0, &no_read, reinterpret_cast<void **>(&mapped)));
  const std::size_t source_pitch =
      static_cast<std::size_t>(game.icon_width) * 4U;
  for (std::uint32_t row = 0; row < game.icon_height; ++row) {
    std::memcpy(
        mapped + footprint.Offset +
            static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
        game.icon_bgra8.data() + static_cast<std::size_t>(row) * source_pitch,
        source_pitch);
  }
  const D3D12_RANGE written_range{0, static_cast<SIZE_T>(upload_size)};
  upload->Unmap(0, &written_range);

  ComPtr<ID3D12CommandAllocator> upload_allocator;
  ComPtr<ID3D12GraphicsCommandList> upload_commands;
  device_context_.CreateDirectCommands(upload_allocator, upload_commands);

  D3D12CommandEncoder encoder(upload_commands.Get(), transfer_stats_);
  encoder.Track(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
  encoder.Track(upload.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
  encoder.CopyBufferToTexture(texture.Get(), upload.Get(), footprint);
  encoder.Transition(texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  winrt::check_hresult(upload_commands->Close());
  // If signaling or waiting fails, keep queued resources and their allocator
  // alive in the renderer until a later successful drain (or process teardown).
  pending_icon_upload_ = {std::move(texture), std::move(upload),
                          std::move(upload_allocator), std::move(upload_commands)};
  const UINT64 upload_ticket =
      device_context_.Submit(pending_icon_upload_.commands.Get());
  device_context_.Wait(upload_ticket);

  D3D12_SHADER_RESOURCE_VIEW_DESC icon_view{};
  icon_view.Format = texture_description.Format;
  icon_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  icon_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  icon_view.Texture2D.MipLevels = 1;
  device_context_.Device()->CreateShaderResourceView(
      pending_icon_upload_.texture.Get(), &icon_view,
      icon_srv_heap_.Cpu(0));
  selected_icon_texture_ = std::move(pending_icon_upload_.texture);
  pending_icon_upload_ = {};
  return true;
}

void D3D12StatusRenderer::CreateShellPipeline() {
  ComPtr<IDxcLibrary> library;
  ComPtr<IDxcCompiler> compiler;
  winrt::check_hresult(DxcCreateInstance(
      CLSID_DxcLibrary, IID_PPV_ARGS(library.ReleaseAndGetAddressOf())));
  winrt::check_hresult(DxcCreateInstance(
      CLSID_DxcCompiler, IID_PPV_ARGS(compiler.ReleaseAndGetAddressOf())));

  ComPtr<IDxcBlobEncoding> source;
  winrt::check_hresult(library->CreateBlobWithEncodingFromPinned(
      ShellShader, static_cast<UINT32>(std::strlen(ShellShader)), DXC_CP_UTF8,
      source.ReleaseAndGetAddressOf()));
  const auto compile_shader = [&](const wchar_t *entry_point,
                                  const wchar_t *target) -> ComPtr<IDxcBlob> {
    const wchar_t *arguments[] = {L"-Ges", L"-O3"};
    ComPtr<IDxcOperationResult> result;
    winrt::check_hresult(compiler->Compile(
        source.Get(), L"xbox_shell.hlsl", entry_point, target, arguments, 2,
        nullptr, 0, nullptr, result.ReleaseAndGetAddressOf()));
    HRESULT status = E_FAIL;
    winrt::check_hresult(result->GetStatus(&status));
    winrt::check_hresult(status);

    ComPtr<IDxcBlob> shader;
    winrt::check_hresult(result->GetResult(shader.ReleaseAndGetAddressOf()));
    return shader;
  };

  const ComPtr<IDxcBlob> vertex_shader = compile_shader(L"VSMain", L"vs_6_0");
  const ComPtr<IDxcBlob> pixel_shader = compile_shader(L"PSMain", L"ps_6_0");
  // The same DXC interfaces used by the Xbox shell compile this host-only probe.
  ComPtr<IDxcBlobEncoding> compute_source;
  winrt::check_hresult(library->CreateBlobWithEncodingFromPinned(
      D3D12ComputeProbeShader,
      static_cast<UINT32>(std::strlen(D3D12ComputeProbeShader)), DXC_CP_UTF8,
      compute_source.ReleaseAndGetAddressOf()));
  source = compute_source;
  const auto compute_shader = compile_shader(L"CSMain", L"cs_6_0");
  compute_probe_passed_ = RunD3D12ComputeProbe(
      device_context_, resource_allocator_, pipeline_cache_,
      {compute_shader->GetBufferPointer(), compute_shader->GetBufferSize()}, &transfer_stats_);
  if (!compute_probe_passed_) {
    winrt::throw_hresult(E_FAIL);
  }

  D3D12_DESCRIPTOR_RANGE icon_range{};
  icon_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  icon_range.NumDescriptors = 1;
  icon_range.BaseShaderRegister = 0;
  icon_range.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  std::array<D3D12_ROOT_PARAMETER, 2> root_parameters{};
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameters[0].Constants.ShaderRegister = 0;
  root_parameters[0].Constants.RegisterSpace = 0;
  root_parameters[0].Constants.Num32BitValues = 12;
  root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  root_parameters[1].DescriptorTable.pDescriptorRanges = &icon_range;
  root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_STATIC_SAMPLER_DESC icon_sampler{};
  icon_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  icon_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  icon_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  icon_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  icon_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  icon_sampler.MaxLOD = D3D12_FLOAT32_MAX;
  icon_sampler.ShaderRegister = 0;
  icon_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC root_description{};
  root_description.NumParameters = static_cast<UINT>(root_parameters.size());
  root_description.pParameters = root_parameters.data();
  root_description.NumStaticSamplers = 1;
  root_description.pStaticSamplers = &icon_sampler;
  root_description.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  root_signature_ = pipeline_cache_.RootSignature(root_description);
  const D3D12_SHADER_BYTECODE vertex{vertex_shader->GetBufferPointer(),
                                    vertex_shader->GetBufferSize()};
  const D3D12_SHADER_BYTECODE pixel{pixel_shader->GetBufferPointer(),
                                   pixel_shader->GetBufferSize()};
  pipeline_state_ = pipeline_cache_.Graphics(
      root_signature_, vertex, pixel, DXGI_FORMAT_B8G8R8A8_UNORM, true);
  // Startup contract check: a repeated request must return the existing PSO.
  if (pipeline_cache_.RootSignature(root_description) != root_signature_ ||
      pipeline_cache_.Graphics(root_signature_, vertex, pixel,
                               DXGI_FORMAT_B8G8R8A8_UNORM, true).Get() !=
          pipeline_state_.Get()) {
    winrt::throw_hresult(E_UNEXPECTED);
  }
}
