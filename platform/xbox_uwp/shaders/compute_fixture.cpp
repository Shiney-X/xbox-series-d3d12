// SPDX-License-Identifier: GPL-2.0-or-later
#include "spirv_hlsl_bridge.h"

#include <initializer_list>
#include <bit>
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

Shader::PushData ProbePushData() {
  Shader::PushData data{};
  data.xoffset = 1;
  data.yoffset = 2;
  data.xscale = 3;
  data.yscale = 4;
  for (std::uint32_t i = 0; i < data.ud_regs.size(); ++i) {
    data.ud_regs[i] = i + 1;
  }
  for (std::uint32_t i = 0; i < data.buf_offsets.size(); ++i) {
    data.buf_offsets[i] = static_cast<std::uint8_t>(i + 1);
  }
  return data;
}

std::array<std::uint32_t, sizeof(Shader::PushData) / 4> EncodePushData(const Shader::PushData &data) {
  static_assert(std::endian::native == std::endian::little);
  return std::bit_cast<std::array<std::uint32_t, sizeof(Shader::PushData) / 4>>(data);
}

std::vector<std::uint32_t> PushDataFixture() {
  // Authored module matching DefinePushDataBlock's real ABI; NOT EmitSPIRV
  // output. Read all 4 floats, 16 user registers and 40 packed byte offsets.
  auto original = ComputeFixture();
  std::vector<std::uint32_t> annotations;
  std::vector<std::uint32_t> declarations;
  std::vector<std::uint32_t> body;
  const auto emit = [](auto &target, spv::Op opcode,
                       std::initializer_list<std::uint32_t> operands) {
    target.push_back((static_cast<std::uint32_t>(operands.size() + 1) << 16) |
                     static_cast<std::uint32_t>(opcode));
    target.insert(target.end(), operands);
  };
  constexpr std::array<std::uint32_t, 11> offsets{0, 4, 8, 12, 16, 32, 48, 64, 80, 96, 112};
  emit(annotations, spv::OpDecorate, {27, spv::DecorationBlock});
  for (std::uint32_t i = 0; i < offsets.size(); ++i) {
    emit(annotations, spv::OpMemberDecorate, {27, i, spv::DecorationOffset, offsets[i]});
  }
  emit(declarations, spv::OpTypeFloat, {26, 32});
  emit(declarations, spv::OpTypeStruct, {27, 26, 26, 26, 26, 23, 23, 23, 23, 23, 23, 4});
  emit(declarations, spv::OpTypePointer, {28, spv::StorageClassPushConstant, 27});
  emit(declarations, spv::OpVariable, {28, 29, spv::StorageClassPushConstant});
  for (std::uint32_t i = 0; i < offsets.size(); ++i) {
    emit(declarations, spv::OpConstant, {2, 30 + i, i});
  }
  emit(declarations, spv::OpConstant, {2, 41, 8});
  emit(declarations, spv::OpConstant, {2, 42, 255});
  emit(declarations, spv::OpTypePointer, {43, spv::StorageClassPushConstant, 26});
  emit(declarations, spv::OpTypePointer, {44, spv::StorageClassPushConstant, 2});
  emit(declarations, spv::OpConstant, {2, 45, 16});
  emit(declarations, spv::OpConstant, {2, 46, 24});
  constexpr std::array<std::uint32_t, 4> shifts{30, 41, 45, 46};
  std::uint32_t next_id = 47;
  std::uint32_t sum = 9; // Original constant 100, before invocation index.
  const auto add = [&](std::uint32_t value) {
    const auto new_sum = next_id++;
    emit(body, spv::OpIAdd, {2, new_sum, sum, value});
    sum = new_sum;
  };
  for (std::uint32_t member = 0; member < 4; ++member) {
    const auto pointer = next_id++;
    const auto loaded = next_id++;
    const auto converted = next_id++;
    emit(body, spv::OpAccessChain, {43, pointer, 29, 30 + member});
    emit(body, spv::OpLoad, {26, loaded, pointer});
    emit(body, spv::OpConvertFToU, {2, converted, loaded});
    add(converted);
  }
  for (std::uint32_t reg = 0; reg < Shader::NUM_USER_DATA_REGS; ++reg) {
    const auto pointer = next_id++;
    const auto loaded = next_id++;
    emit(body, spv::OpAccessChain, {44, pointer, 29, 34 + reg / 4, 30 + reg % 4});
    emit(body, spv::OpLoad, {2, loaded, pointer});
    add(loaded);
  }
  for (std::uint32_t byte = 0; byte < Shader::NUM_BUFFERS; ++byte) {
    const auto pointer = next_id++;
    const auto loaded = next_id++;
    const auto shifted = next_id++;
    const auto unpacked = next_id++;
    const auto word = byte / 4;
    emit(body, spv::OpAccessChain, {44, pointer, 29, 38 + word / 4, 30 + word % 4});
    emit(body, spv::OpLoad, {2, loaded, pointer});
    emit(body, spv::OpShiftRightLogical, {2, shifted, loaded, shifts[byte % 4]});
    emit(body, spv::OpBitwiseAnd, {2, unpacked, shifted, 42});
    add(unpacked);
  }
  std::vector<std::uint32_t> result(original.begin(), original.begin() + 5);
  result[3] = next_id;
  for (std::size_t offset = 5; offset < original.size();) {
    const auto count = original[offset] >> 16;
    const auto opcode = original[offset] & 0xffff;
    if (opcode == spv::OpTypeVoid) {
      result.insert(result.end(), annotations.begin(), annotations.end());
    }
    if (opcode == spv::OpFunction) {
      result.insert(result.end(), declarations.begin(), declarations.end());
    }
    if (opcode == spv::OpIAdd && original[offset + 2] == 20) {
      result.insert(result.end(), body.begin(), body.end());
      original[offset + 3] = sum;
    }
    result.insert(result.end(), original.begin() + static_cast<std::ptrdiff_t>(offset),
                  original.begin() + static_cast<std::ptrdiff_t>(offset + count));
    offset += count;
  }
  return result;
}
} // namespace Xbox::Shaders
