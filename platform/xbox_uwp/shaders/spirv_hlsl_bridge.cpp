// SPDX-License-Identifier: GPL-2.0-or-later
#include "spirv_hlsl_bridge.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <spirv_hlsl.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <stdexcept>

namespace Xbox::Shaders {
ComputeTranslation TranslateCompute(std::span<const std::uint32_t> words) {
  constexpr std::size_t MaxWords = 65536;
  if (words.size() < 5 || words.size() > MaxWords || words[0] != 0x07230203 ||
      words[1] < 0x00010000 || words[1] > 0x00010600 || words[3] == 0 ||
      words[3] > MaxWords || words[4] != 0) {
    throw std::invalid_argument("invalid or oversized SPIR-V header");
  }
  for (std::size_t offset = 5; offset < words.size();) {
    const auto count = words[offset] >> 16;
    if (count == 0 || count > words.size() - offset) {
      throw std::invalid_argument("truncated SPIR-V instruction");
    }
    offset += count;
  }
  spirv_cross::CompilerHLSL compiler(words.data(), words.size());
  const auto entries = compiler.get_entry_points_and_stages();
  if (entries.size() != 1 || entries.front().execution_model != spv::ExecutionModelGLCompute) {
    throw std::invalid_argument("only one compute entry point is supported");
  }
  compiler.set_entry_point(entries.front().name, spv::ExecutionModelGLCompute);
  const auto resources = compiler.get_shader_resources();
  if (resources.storage_images.size() != 1 || !resources.uniform_buffers.empty() ||
      !resources.storage_buffers.empty() || !resources.sampled_images.empty() ||
      !resources.separate_images.empty() || !resources.separate_samplers.empty() ||
      !resources.push_constant_buffers.empty() || !resources.subpass_inputs.empty() ||
      !resources.atomic_counters.empty() || !resources.acceleration_structures.empty() ||
      !resources.stage_inputs.empty() || !resources.stage_outputs.empty()) {
    throw std::invalid_argument("unsupported compute resource layout");
  }
  const auto &image = resources.storage_images.front();
  const auto &type = compiler.get_type(image.type_id);
  const auto &sampled = compiler.get_type(type.image.type);
  if (!type.array.empty() || type.image.dim != spv::Dim2D || type.image.arrayed ||
      type.image.ms || type.image.format != spv::ImageFormatR32ui ||
      sampled.basetype != spirv_cross::SPIRType::UInt || sampled.width != 32 ||
      !compiler.has_decoration(image.id, spv::DecorationDescriptorSet) ||
      !compiler.has_decoration(image.id, spv::DecorationBinding) ||
      compiler.get_decoration(image.id, spv::DecorationDescriptorSet) != 0 ||
      compiler.get_decoration(image.id, spv::DecorationBinding) != 0) {
    throw std::invalid_argument("expected scalar R32_UINT image at set 0 binding 0");
  }
  ComputeTranslation result;
  if (!compiler.get_execution_mode_bitset().get(spv::ExecutionModeLocalSize) ||
      compiler.get_execution_mode_bitset().get(spv::ExecutionModeLocalSizeId)) {
    throw std::invalid_argument("literal local size required");
  }
  std::uint64_t threads = 1;
  for (std::uint32_t axis = 0; axis < 3; ++axis) {
    result.local_size[axis] = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, axis);
    if (result.local_size[axis] == 0 || result.local_size[axis] > 1024) {
      throw std::invalid_argument("invalid workgroup size");
    }
    threads *= result.local_size[axis];
  }
  if (threads > 1024 || result.local_size[2] > 64) {
    throw std::invalid_argument("workgroup exceeds cs_6_0 limits");
  }
  spirv_cross::CompilerHLSL::Options options;
  options.shader_model = 60;
  compiler.set_hlsl_options(options);
  spirv_cross::HLSLResourceBinding mapping{};
  mapping.stage = spv::ExecutionModelGLCompute;
  mapping.desc_set = 0;
  mapping.binding = 0;
  mapping.uav.register_binding = 0;
  mapping.uav.register_space = 0;
  compiler.add_hlsl_resource_binding(mapping);
  result.hlsl = compiler.compile();
  if (result.hlsl.empty()) {
    throw std::runtime_error("SPIRV-Cross emitted empty HLSL");
  }
  return result;
}
} // namespace Xbox::Shaders
