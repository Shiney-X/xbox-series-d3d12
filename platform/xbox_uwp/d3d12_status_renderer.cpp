// SPDX-License-Identifier: GPL-2.0-or-later

#include "d3d12_status_renderer.h"

#include <dxcapi.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>

namespace {

constexpr char ShellShader[] = R"(
cbuffer DrawConstants : register(b0) {
    float4 rect;
    float4 draw_color;
};

struct VertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
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
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target {
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
  if (fence_event_ != INVALID_HANDLE_VALUE) {
    CloseHandle(fence_event_);
  }
}

void D3D12StatusRenderer::Initialize(IUnknown *core_window, float width,
                                     float height) {
  winrt::check_hresult(
      D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                        IID_PPV_ARGS(device_.ReleaseAndGetAddressOf())));

  D3D12_COMMAND_QUEUE_DESC queue_description{};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  winrt::check_hresult(device_->CreateCommandQueue(
      &queue_description,
      IID_PPV_ARGS(command_queue_.ReleaseAndGetAddressOf())));

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

  viewport_.Width = static_cast<float>(swap_chain_description.Width);
  viewport_.Height = static_cast<float>(swap_chain_description.Height);
  viewport_.MaxDepth = 1.0F;
  scissor_.right = static_cast<LONG>(swap_chain_description.Width);
  scissor_.bottom = static_cast<LONG>(swap_chain_description.Height);

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
    device_->CreateRenderTargetView(render_targets_[index].Get(), nullptr,
                                    rtv_handle);
    rtv_handle.ptr += rtv_descriptor_size_;
  }

  winrt::check_hresult(device_->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      IID_PPV_ARGS(command_allocator_.ReleaseAndGetAddressOf())));
  winrt::check_hresult(device_->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator_.Get(), nullptr,
      IID_PPV_ARGS(command_list_.ReleaseAndGetAddressOf())));
  winrt::check_hresult(command_list_->Close());

  winrt::check_hresult(device_->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.ReleaseAndGetAddressOf())));
  fence_event_ = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  if (fence_event_ == nullptr) {
    fence_event_ = INVALID_HANDLE_VALUE;
    winrt::throw_last_error();
  }

  CreateShellPipeline();
}

void D3D12StatusRenderer::Render(const XboxShellState &state) {
  winrt::check_hresult(command_allocator_->Reset());
  winrt::check_hresult(
      command_list_->Reset(command_allocator_.Get(), pipeline_state_.Get()));

  const UINT frame_index = swap_chain_->GetCurrentBackBufferIndex();
  D3D12_RESOURCE_BARRIER begin_barrier{};
  begin_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  begin_barrier.Transition.pResource = render_targets_[frame_index].Get();
  begin_barrier.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  begin_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  begin_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  command_list_->ResourceBarrier(1, &begin_barrier);

  auto rtv_handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  rtv_handle.ptr += static_cast<SIZE_T>(frame_index) * rtv_descriptor_size_;
  constexpr float background[] = {0.012F, 0.022F, 0.042F, 1.0F};
  command_list_->ClearRenderTargetView(rtv_handle, background, 0, nullptr);
  command_list_->SetGraphicsRootSignature(root_signature_.Get());
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

  D3D12_RESOURCE_BARRIER end_barrier = begin_barrier;
  end_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  end_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  command_list_->ResourceBarrier(1, &end_barrier);

  winrt::check_hresult(command_list_->Close());
  ID3D12CommandList *command_lists[] = {command_list_.Get()};
  command_queue_->ExecuteCommandLists(1, command_lists);
  winrt::check_hresult(swap_chain_->Present(1, 0));
  WaitForGpu();
}

void D3D12StatusRenderer::DrawRectangle(float x, float y, float width,
                                        float height,
                                        const std::array<float, 4> &color) {
  const std::array<float, 8> constants{x,        y,        width,    height,
                                       color[0], color[1], color[2], color[3]};
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
      DrawText("RESTORING GAME FOLDER", 0.105F, 0.31F, 5.0F, PrimaryText);
      DrawText("CHECKING SAVED ACCESS TOKEN", 0.105F, 0.43F, 4.0F,
               SecondaryText);
      break;
    case LibraryFolderState::Picking:
      DrawText("FOLDER PICKER OPEN", 0.105F, 0.31F, 5.0F, PrimaryText);
      DrawText("CHOOSE A PS4 GAME FOLDER", 0.105F, 0.43F, 4.0F, SecondaryText);
      break;
    case LibraryFolderState::Ready:
      DrawText("GAME FOLDER READY", 0.105F, 0.29F, 5.5F, Accent);
      DrawText("FOLDER  " + state.library_folder_name, 0.105F, 0.41F, 4.5F,
               PrimaryText);
      DrawText("A CHANGE FOLDER", 0.105F, 0.55F, 4.0F, SecondaryText);
      break;
    case LibraryFolderState::Failed:
      DrawText("FOLDER ACCESS FAILED", 0.105F, 0.29F, 5.5F, Failure);
      DrawText("SEE PHASE1-LIBRARY.JSONL", 0.105F, 0.43F, 4.0F, SecondaryText);
      DrawText("A TRY AGAIN", 0.105F, 0.56F, 4.0F, PrimaryText);
      break;
    case LibraryFolderState::Cancelled:
      DrawText("FOLDER SELECTION CANCELLED", 0.105F, 0.29F, 5.0F, PrimaryText);
      DrawText("A TRY AGAIN", 0.105F, 0.46F, 4.5F, SecondaryText);
      break;
    case LibraryFolderState::NotConfigured:
    default:
      DrawText("NO GAME FOLDER", 0.105F, 0.29F, 5.5F, PrimaryText);
      DrawText("A ADD FOLDER", 0.105F, 0.43F, 5.0F, Accent);
      DrawText("SELECT A FOLDER WITH PS4 DUMPS", 0.105F, 0.56F, 3.8F,
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

  DrawText(state.page == XboxShellPage::Games ? "A FOLDER   B BACK" : "B BACK",
           0.065F, 0.865F, 4.0F, SecondaryText);
}

bool D3D12StatusRenderer::TryTrim() {
  ComPtr<IDXGIDevice3> dxgi_device;
  const HRESULT result = device_.As(&dxgi_device);
  if (result == E_NOINTERFACE) {
    return false;
  }
  winrt::check_hresult(result);
  dxgi_device->Trim();
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

  D3D12_ROOT_SIGNATURE_DESC root_description{};
  D3D12_ROOT_PARAMETER root_parameter{};
  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameter.Constants.ShaderRegister = 0;
  root_parameter.Constants.RegisterSpace = 0;
  root_parameter.Constants.Num32BitValues = 8;
  root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_description.NumParameters = 1;
  root_description.pParameters = &root_parameter;
  root_description.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> serialized_root;
  ComPtr<ID3DBlob> diagnostics;
  winrt::check_hresult(D3D12SerializeRootSignature(
      &root_description, D3D_ROOT_SIGNATURE_VERSION_1,
      serialized_root.ReleaseAndGetAddressOf(),
      diagnostics.ReleaseAndGetAddressOf()));
  winrt::check_hresult(device_->CreateRootSignature(
      0, serialized_root->GetBufferPointer(), serialized_root->GetBufferSize(),
      IID_PPV_ARGS(root_signature_.ReleaseAndGetAddressOf())));

  D3D12_BLEND_DESC blend{};
  blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  blend.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
  blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
  blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

  D3D12_RASTERIZER_DESC rasterizer{};
  rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
  rasterizer.CullMode = D3D12_CULL_MODE_NONE;
  rasterizer.DepthClipEnable = TRUE;

  D3D12_DEPTH_STENCIL_DESC depth_stencil{};
  depth_stencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  depth_stencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  depth_stencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
  depth_stencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
  depth_stencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
  depth_stencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  depth_stencil.BackFace = depth_stencil.FrontFace;

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
  pipeline.pRootSignature = root_signature_.Get();
  pipeline.VS = {vertex_shader->GetBufferPointer(),
                 vertex_shader->GetBufferSize()};
  pipeline.PS = {pixel_shader->GetBufferPointer(),
                 pixel_shader->GetBufferSize()};
  pipeline.BlendState = blend;
  pipeline.SampleMask = UINT_MAX;
  pipeline.RasterizerState = rasterizer;
  pipeline.DepthStencilState = depth_stencil;
  pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline.NumRenderTargets = 1;
  pipeline.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
  pipeline.SampleDesc.Count = 1;
  winrt::check_hresult(device_->CreateGraphicsPipelineState(
      &pipeline, IID_PPV_ARGS(pipeline_state_.ReleaseAndGetAddressOf())));
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
