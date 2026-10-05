// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "probes.h"

// Only executes the authored fixture; no path/address/guest code accepted from
// callers.
[[nodiscard]] XboxSeriesD3D12::Phase0::ProbeResult ProbeGuestExecution();
