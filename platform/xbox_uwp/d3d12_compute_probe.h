// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "d3d12_device_context.h"
#include "d3d12_pipeline_cache.h"
#include "d3d12_resource_allocator.h"
#include "d3d12_command_encoder.h"

inline constexpr char D3D12ComputeProbeShader[] = R"(
RWTexture2D<uint> output_values : register(u0);
[numthreads(2, 2, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  output_values[id.xy] = 100 + id.x + 2 * id.y;
}
)";

// Startup-only synthetic dispatch/readback; never executes guest code.
[[nodiscard]] bool RunD3D12ComputeProbe(D3D12DeviceContext &context,
                                       D3D12ResourceAllocator &allocator,
                                       D3D12PipelineCache &cache,
                                       D3D12_SHADER_BYTECODE shader,
                                       D3D12TransferStats *stats = nullptr);
