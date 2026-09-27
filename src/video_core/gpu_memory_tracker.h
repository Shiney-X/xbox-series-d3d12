// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace VideoCore {

/**
 * Backend-neutral boundary used to keep CPU virtual-memory changes coherent
 * with resources owned by the active graphics backend.
 *
 * This deliberately exposes guest addresses instead of Vulkan or D3D12
 * handles. Backend-specific caches remain responsible for translating those
 * addresses to their native resources.
 */
class GpuMemoryTracker {
public:
    virtual ~GpuMemoryTracker() = default;

    virtual bool InvalidateMemory(VAddr addr, u64 size) = 0;
    virtual bool ReadMemory(VAddr addr, u64 size) = 0;
    virtual bool IsMapped(VAddr addr, u64 size) = 0;
    virtual void MapMemory(VAddr addr, u64 size) = 0;
    virtual void UnmapMemory(VAddr addr, u64 size) = 0;
};

} // namespace VideoCore
