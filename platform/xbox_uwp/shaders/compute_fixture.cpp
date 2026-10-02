// SPDX-License-Identifier: GPL-2.0-or-later
#include "spirv_hlsl_bridge.h"

#include <initializer_list>
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <spirv.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Xbox::Shaders {
std::vector<std::uint32_t> ComputeFixture() {
  // Authored SPIR-V 1.0 fixture, NOT emitted from PS4 ISA. All IDs are local.
  // output[GlobalInvocationId.xy] = 100 + x + 2*y; LocalSize=(2,2,1).
  std::vector<std::uint32_t> words{0x07230203, 0x00010000, 0, 26, 0};
  const auto emit = [&](spv::Op opcode, std::initializer_list<std::uint32_t> operands) {
    words.push_back((static_cast<std::uint32_t>(operands.size() + 1) << 16) |
                    static_cast<std::uint32_t>(opcode));
    words.insert(words.end(), operands);
  };
  emit(spv::OpCapability, {spv::CapabilityShader});
  emit(spv::OpMemoryModel, {spv::AddressingModelLogical, spv::MemoryModelGLSL450});
  emit(spv::OpEntryPoint, {spv::ExecutionModelGLCompute, 13, 0x6e69616d, 0, 11});
  emit(spv::OpExecutionMode, {13, spv::ExecutionModeLocalSize, 2, 2, 1});
  emit(spv::OpDecorate, {11, spv::DecorationBuiltIn, spv::BuiltInGlobalInvocationId});
  emit(spv::OpDecorate, {12, spv::DecorationDescriptorSet, 0});
  emit(spv::OpDecorate, {12, spv::DecorationBinding, 0});
  emit(spv::OpTypeVoid, {1});
  emit(spv::OpTypeInt, {2, 32, 0});
  emit(spv::OpTypeVector, {3, 2, 3});
  emit(spv::OpTypeVector, {4, 2, 2});
  emit(spv::OpTypePointer, {5, spv::StorageClassInput, 3});
  emit(spv::OpTypeImage, {6, 2, spv::Dim2D, 0, 0, 0, 2, spv::ImageFormatR32ui});
  emit(spv::OpTypePointer, {7, spv::StorageClassUniformConstant, 6});
  emit(spv::OpTypeFunction, {8, 1});
  emit(spv::OpConstant, {2, 9, 100});
  emit(spv::OpConstant, {2, 10, 2});
  emit(spv::OpTypeVector, {23, 2, 4});
  emit(spv::OpConstant, {2, 24, 0});
  emit(spv::OpVariable, {5, 11, spv::StorageClassInput});
  emit(spv::OpVariable, {7, 12, spv::StorageClassUniformConstant});
  emit(spv::OpFunction, {1, 13, spv::FunctionControlMaskNone, 8});
  emit(spv::OpLabel, {14});
  emit(spv::OpLoad, {3, 15, 11});
  emit(spv::OpCompositeExtract, {2, 16, 15, 0});
  emit(spv::OpCompositeExtract, {2, 17, 15, 1});
  emit(spv::OpIMul, {2, 18, 10, 17});
  emit(spv::OpIAdd, {2, 19, 16, 18});
  emit(spv::OpIAdd, {2, 20, 9, 19});
  emit(spv::OpCompositeConstruct, {4, 21, 16, 17});
  emit(spv::OpLoad, {6, 22, 12});
  emit(spv::OpCompositeConstruct, {23, 25, 20, 24, 24, 24});
  emit(spv::OpImageWrite, {22, 21, 25});
  emit(spv::OpReturn, {});
  emit(spv::OpFunctionEnd, {});
  return words;
}
} // namespace Xbox::Shaders
