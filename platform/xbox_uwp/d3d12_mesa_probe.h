// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "d3d12_compute_probe.h"
#include <string>

struct D3D12MesaProbe {
  bool passed{}, compute{}, graphics{}, rejects_invalid{};
  std::string details;
};
// Real GPU dispatch/draw/readback of authored shadPS4 IR via Mesa. No guest
// executable/PM4 integration and no automatic HLSL fallback on failure.
[[nodiscard]] D3D12MesaProbe
RunD3D12MesaProbe(D3D12DeviceContext &context,
                  D3D12ResourceAllocator &allocator, D3D12PipelineCache &cache);
