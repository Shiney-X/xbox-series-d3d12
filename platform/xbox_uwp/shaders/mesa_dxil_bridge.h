// SPDX-FileCopyrightText: 2026 KytyPS5 contributors
// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct ID3D12Device;

namespace Xbox::Shaders {
enum class MesaStage { Compute, Vertex, Fragment };
struct MesaStageInput {
  std::span<const std::uint32_t> words;
  MesaStage stage{};
  std::uint32_t push_constant_space{};
  // Only for draws explicitly known to have firstVertex/baseInstance == 0.
  bool zero_base_vertex{};
};
struct MesaShader {
  std::vector<std::uint8_t> dxil;
  bool requires_runtime_data{}, needs_draw_sysvals{};
};
struct MesaTranslation {
  bool passed{};
  std::uint32_t shader_model_minor{}, validator_minor{};
  bool lower_16bit{};
  std::vector<MesaShader> shaders;
  std::string error;
  std::string diagnostics;
};
// Adapted from KytyPS5 dxilCompiler.cpp at
// cd0c360aa7b432645b915c19fd1ceed394378b75. One compute stage or linked
// vertex/fragment stages, no Vulkan host API. Validates/signs every result
// through the packaged SDK dxil.dll. No fallback that treats an unsigned/failed
// shader as successfully translated.
[[nodiscard]] MesaTranslation
TranslateMesaDxil(ID3D12Device *device, std::span<const MesaStageInput> stages);
} // namespace Xbox::Shaders
