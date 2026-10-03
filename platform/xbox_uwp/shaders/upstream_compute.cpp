// SPDX-FileCopyrightText: Copyright 2026 Shiney-X and xbox-series-d3d12
// contributors SPDX-License-Identifier: GPL-2.0-or-later
#include "upstream_compute.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"

namespace Xbox::Shaders {
std::vector<std::uint32_t> EmitUpstreamGraphics(bool vertex) {
  using namespace Shader;
  Info info{};
  info.stage = vertex ? Stage::Vertex : Stage::Fragment;
  info.l_stage = vertex ? LogicalStage::Vertex : LogicalStage::Fragment;
  info.ud_mask.Set(IR::ScalarReg::S0);
  for (u32 c = 0; c < 4; ++c) {
    info.stores.Set(
        vertex ? IR::Attribute::Param0 : IR::Attribute::RenderTarget0, c);
    if (vertex)
      info.stores.Set(IR::Attribute::Position0, c);
    else
      info.loads.Set(IR::Attribute::Param0, c);
  }
  Common::ObjectPool<IR::Inst> pool{64};
  IR::Block block{pool};
  struct Lifetime {
    IR::Block &block;
    ~Lifetime() {
      for (auto &inst : block.Instructions())
        inst.ClearArgs();
      block.Instructions().clear_and_dispose(
          [](IR::Inst *inst) { std::destroy_at(inst); });
    }
  } lifetime{block};
  IR::Program program{info};
  program.blocks.push_back(&block);
  program.post_order_blocks.push_back(&block);
  IR::AbstractSyntaxNode node{};
  node.type = IR::AbstractSyntaxNode::Type::Block;
  node.data.block = &block;
  program.syntax_list.push_back(node);
  node.type = IR::AbstractSyntaxNode::Type::Return;
  program.syntax_list.push_back(node);
  IR::IREmitter ir{block};
  ir.Prologue();
  const auto zero = ir.Imm32(0.0f);
  const auto one = ir.Imm32(1.0f);
  const auto constant = ir.BitCast<IR::F32>(ir.GetUserData(IR::ScalarReg::S0));
  if (vertex) {
    const auto id = ir.GetAttributeU32(IR::Attribute::VertexId);
    const IR::F32 x{ir.Select(ir.IEqual(id, ir.Imm32(2u)), ir.Imm32(3.0f),
                              ir.Imm32(-1.0f))};
    const IR::F32 y{ir.Select(ir.IEqual(id, ir.Imm32(1u)), ir.Imm32(3.0f),
                              ir.Imm32(-1.0f))};
    ir.SetAttribute(IR::Attribute::Position0, x, 0);
    ir.SetAttribute(IR::Attribute::Position0, y, 1);
    ir.SetAttribute(IR::Attribute::Position0, zero, 2);
    ir.SetAttribute(IR::Attribute::Position0, one, 3);
    ir.SetAttribute(IR::Attribute::Param0, constant, 0);
    ir.SetAttribute(IR::Attribute::Param0, zero, 1);
    ir.SetAttribute(IR::Attribute::Param0, zero, 2);
    ir.SetAttribute(IR::Attribute::Param0, one, 3);
  } else {
    ir.SetAttribute(IR::Attribute::RenderTarget0,
                    ir.GetAttribute(IR::Attribute::Param0, 0), 0);
    ir.SetAttribute(IR::Attribute::RenderTarget0, constant, 1);
    ir.SetAttribute(IR::Attribute::RenderTarget0, zero, 2);
    ir.SetAttribute(IR::Attribute::RenderTarget0,
                    ir.GetAttribute(IR::Attribute::Param0, 3), 3);
  }
  ir.Epilogue();
  Profile profile{};
  profile.supported_spirv = 0x00010300;
  profile.subgroup_size = 32;
  RuntimeInfo runtime{};
  runtime.Initialize(info.stage);
  if (!vertex) {
    runtime.fs_info.num_inputs = 1;
    runtime.fs_info.inputs[0].param_index = 0;
    runtime.fs_info.color_buffers[0].num_format = AmdGpu::NumberFormat::Float;
  }
  Backend::Bindings bindings{};
  return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
}
std::vector<std::uint32_t> EmitUpstreamCompute(std::uint32_t base) {
  using namespace Shader;
  Info info{};
  info.stage = Stage::Compute;
  info.l_stage = LogicalStage::Compute;
  AmdGpu::Image image{};
  image.base_address = 1;
  image.type = u64(AmdGpu::ImageType::Color2D);
  image.data_format = u64(AmdGpu::DataFormat::Format32);
  image.num_format = u64(AmdGpu::NumberFormat::Uint);
  image.width = 1;
  image.height = 1;
  info.flattened_ud_buf.resize(sizeof(image) / sizeof(u32));
  std::memcpy(info.flattened_ud_buf.data(), &image, sizeof(image));
  info.images.push_back(
      ImageResource{.sharp_idx = 0, .is_atomic = true, .is_written = true});
  info.loads.Set(IR::Attribute::LocalInvocationId, 0);
  info.loads.Set(IR::Attribute::LocalInvocationId, 1);
  Common::ObjectPool<IR::Inst> pool{64};
  IR::Block block{pool};
  // ObjectPool stores objects in union slots; the standalone probe explicitly
  // releases IR use lists before releasing its storage, including on
  // exceptions.
  struct BlockLifetime {
    IR::Block &block;
    ~BlockLifetime() {
      for (auto &inst : block.Instructions()) {
        inst.ClearArgs();
      }
      block.Instructions().clear_and_dispose(
          [](IR::Inst *inst) { std::destroy_at(inst); });
    }
  } lifetime{block};
  IR::Program program{info};
  program.blocks.push_back(&block);
  program.post_order_blocks.push_back(&block);
  IR::AbstractSyntaxNode node{};
  node.type = IR::AbstractSyntaxNode::Type::Block;
  node.data.block = &block;
  program.syntax_list.push_back(node);
  node.type = IR::AbstractSyntaxNode::Type::Return;
  program.syntax_list.push_back(node);
  IR::IREmitter ir{block};
  ir.Prologue();
  const auto x = ir.GetAttributeU32(IR::Attribute::LocalInvocationId, 0);
  const auto y = ir.GetAttributeU32(IR::Attribute::LocalInvocationId, 1);
  const IR::U32 twice_y{ir.IMul(y, ir.Imm32(2u))};
  const IR::U32 sum{ir.IAdd(ir.Imm32(base), IR::U32{ir.IAdd(x, twice_y)})};
  const auto coords = ir.CompositeConstruct(x, y);
  const auto color = ir.CompositeConstruct(
      ir.BitCast<IR::F32>(sum), ir.Imm32(0.0f), ir.Imm32(0.0f), ir.Imm32(0.0f));
  ir.ImageWrite(ir.Imm32(0u), coords, {}, {}, color, {});
  ir.Epilogue();
  Profile profile{};
  profile.supported_spirv = 0x00010300;
  profile.subgroup_size = 32;
  RuntimeInfo runtime{};
  runtime.Initialize(Stage::Compute);
  runtime.cs_info.workgroup_size = {2, 2, 1};
  Backend::Bindings bindings{};
  return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
}
} // namespace Xbox::Shaders
