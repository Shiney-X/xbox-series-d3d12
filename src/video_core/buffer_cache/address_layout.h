// SPDX-FileCopyrightText: Copyright 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/types.h"
namespace VideoCore::BufferAddressLayout {
inline constexpr u32 CachingPageBits = 14;
inline constexpr u64 CachingPageSize = u64{1} << CachingPageBits;
} // namespace VideoCore::BufferAddressLayout
