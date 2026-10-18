// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "d3d12_mesa_probe.h"
#include "d3d12_graphics_probe.h"
#include "shaders/mesa_dxil_bridge.h"
#include "shaders/spirv_hlsl_bridge.h"
#include "shaders/upstream_compute.h"
#include <array>
#include <exception>

D3D12MesaProbe RunD3D12MesaProbe(D3D12DeviceContext &context,
                                 D3D12ResourceAllocator &allocator,
                                 D3D12PipelineCache &cache) {
  using namespace Xbox::Shaders;
  D3D12MesaProbe out;
  const auto clean = [](std::string text) {
    for (auto &c : text)
      if (c == ';' || c == '\r' || c == '\n' || c == '=')
        c = ' ';
    return text;
  };
  out.details = "translator=Mesa.spirv_to_dxil;hlsl_intermediate=0;"
                "mesa_revision=e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae;"
                "donor_revision=cd0c360aa7b432645b915c19fd1ceed394378b75;"
                "source=authored_shadps4_ir;upstream_emitter_linked=1;"
                "guest_isa=0;guest_runtime_linked=0;game_frame=0;"
                "scope=compute_and_linked_vs_ps_readback;";
  try {
    const auto compute = EmitUpstreamCompute();
    const std::array inputs{
        MesaStageInput{compute, MesaStage::Compute, 0, false}};
    const auto cs = TranslateMesaDxil(context.Device(), inputs);
    out.details +=
        "shader_model_minor=" + std::to_string(cs.shader_model_minor) +
        ";validator_minor=" + std::to_string(cs.validator_minor) +
        ";lower_16bit=" + std::to_string(cs.lower_16bit) + ";";
    out.details +=
        "compute_compiler_diagnostics=" + clean(cs.diagnostics) + ";";
    if (!cs.passed)
      throw std::runtime_error(cs.error);
    // These closed probes do not bind Mesa runtime data. Never silently run a
    // program whose translator metadata requires it.
    if (cs.shaders[0].requires_runtime_data || cs.shaders[0].needs_draw_sysvals)
      throw std::runtime_error("mesa_compute_runtime_binding_required");
    const auto push = EncodePushData(ProbePushData());
    out.compute = RunD3D12ComputeProbe(
        context, allocator, cache,
        {cs.shaders[0].dxil.data(), cs.shaders[0].dxil.size()}, nullptr, push);
    const auto vertex = EmitUpstreamGraphics(true);
    const auto fragment = EmitUpstreamGraphics(false);
    const std::array graphics{
        MesaStageInput{vertex, MesaStage::Vertex, 1, true},
        MesaStageInput{fragment, MesaStage::Fragment, 2, true}};
    const auto vs_ps = TranslateMesaDxil(context.Device(), graphics);
    out.details +=
        "graphics_compiler_diagnostics=" + clean(vs_ps.diagnostics) + ";";
    if (!vs_ps.passed)
      throw std::runtime_error(vs_ps.error);
    for (const auto &shader : vs_ps.shaders)
      if (shader.requires_runtime_data || shader.needs_draw_sysvals)
        throw std::runtime_error("mesa_graphics_runtime_binding_required");
    out.graphics = RunD3D12GraphicsProbe(
        context, allocator, cache,
        {vs_ps.shaders[0].dxil.data(), vs_ps.shaders[0].dxil.size()},
        {vs_ps.shaders[1].dxil.data(), vs_ps.shaders[1].dxil.size()});
    const std::array bad{MesaStageInput{std::span<const std::uint32_t>{},
                                        MesaStage::Compute, 0, false}};
    const auto malformed = TranslateMesaDxil(context.Device(), bad);
    const auto reversed = std::array{graphics[1], graphics[0]};
    const auto wrong_order = TranslateMesaDxil(context.Device(), reversed);
    out.rejects_invalid = !malformed.passed && malformed.shaders.empty() &&
                          !wrong_order.passed && wrong_order.shaders.empty();
    out.passed = out.compute && out.graphics && out.rejects_invalid;
  } catch (const std::exception &error) {
    // Escape separators so a compiler log cannot masquerade as report fields.
    std::string message = error.what();
    for (auto &c : message)
      if (c == ';' || c == '\r' || c == '\n' || c == '=')
        c = ' ';
    out.details += "error=" + message + ";";
  }
  out.details +=
      "compute_readback_verified=" + std::to_string(out.compute) +
      ";linked_graphics_readback_verified=" + std::to_string(out.graphics) +
      ";invalid_rejected=" + std::to_string(out.rejects_invalid);
  return out;
}
