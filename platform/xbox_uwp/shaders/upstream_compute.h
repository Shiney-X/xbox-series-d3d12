// SPDX-FileCopyrightText: Copyright 2026 Shiney-X and xbox-series-d3d12
// contributors SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <vector>
namespace Xbox::Shaders {
// Authored IR, not guest ISA. One fullscreen triangle and one float4 varying.
[[nodiscard]] std::vector<std::uint32_t> EmitUpstreamGraphics(bool vertex);
// Authored IR, emitted by the unmodified instruction dispatch and real
// EmitSPIRV backend. Not guest GCN, not a decoded game shader, and no
// runtime/PM4 integration yet.
std::vector<std::uint32_t> EmitUpstreamCompute(std::uint32_t base = 100);
} // namespace Xbox::Shaders
