// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "d3d12_command_encoder.h"
#include "d3d12_device_context.h"
#include "d3d12_resource_allocator.h"

struct D3D12TransferProbeResult {
  bool passed{};
  bool resolve_supported{};
  UINT sample_count{};
};

// Real GPU buffer copies, padded texture upload/readback, color/depth clear,
// and MSAA color resolve when advertised by the device. No shaders/guest code.
[[nodiscard]] D3D12TransferProbeResult
RunD3D12TransferProbe(D3D12DeviceContext &context,
                      D3D12ResourceAllocator &allocator,
                      D3D12TransferStats &stats);
