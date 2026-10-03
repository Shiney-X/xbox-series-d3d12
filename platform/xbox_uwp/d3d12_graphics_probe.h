// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "d3d12_command_encoder.h"
#include "d3d12_device_context.h"
#include "d3d12_pipeline_cache.h"
#include "d3d12_resource_allocator.h"

// Two draws with distinct VS/PS PushData: red then green, all 4x4 pixels.
// The shader code must consume VS user_data[0] as R and PS user_data[0] as G.
bool RunD3D12GraphicsProbe(D3D12DeviceContext &context,
                           D3D12ResourceAllocator &allocator,
                           D3D12PipelineCache &cache,
                           D3D12_SHADER_BYTECODE vertex,
                           D3D12_SHADER_BYTECODE fragment,
                           D3D12TransferStats *stats = nullptr);
