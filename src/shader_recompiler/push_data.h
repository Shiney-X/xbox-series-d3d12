// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <type_traits>
#include "common/types.h"

namespace Shader {
static constexpr u32 NUM_USER_DATA_REGS = 16;
static constexpr u32 NUM_BUFFERS = 40;

// Data-only ABI shared by the SPIR-V emitter and host backends. The existing
// AddOffset helper is defined in resource.h with the renderer's ASSERT policy.
struct PushData {
    static constexpr u32 XOffsetIndex = 0;
    static constexpr u32 YOffsetIndex = 1;
    static constexpr u32 XScaleIndex = 2;
    static constexpr u32 YScaleIndex = 3;
    static constexpr u32 UdRegsIndex = 4;
    static constexpr u32 BufOffsetIndex = UdRegsIndex + NUM_USER_DATA_REGS / 4;

    float xoffset;
    float yoffset;
    float xscale;
    float yscale;
    std::array<u32, NUM_USER_DATA_REGS> ud_regs;
    std::array<u8, NUM_BUFFERS> buf_offsets;

    void AddOffset(u32 binding, u32 offset);
};
static_assert(std::is_standard_layout_v<PushData> && std::is_trivially_copyable_v<PushData>);
static_assert(sizeof(PushData) == 120 && alignof(PushData) == 4);
static_assert(offsetof(PushData, ud_regs) == 16 && offsetof(PushData, buf_offsets) == 80);
} // namespace Shader
