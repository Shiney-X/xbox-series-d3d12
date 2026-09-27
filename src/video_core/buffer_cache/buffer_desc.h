// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace VideoCore {

// Preferred placement and CPU visibility for a host buffer allocation.
enum class MemoryUsage {
    DeviceLocal,
    Upload,
    Download,
    Stream,
};

// Capabilities requested by the emulator, independent of the host API's
// buffer-usage or resource-state flags.
enum class BufferUsage : u32 {
    TransferSource = 1u << 0,
    TransferDestination = 1u << 1,
    Uniform = 1u << 2,
    Storage = 1u << 3,
    Index = 1u << 4,
    Vertex = 1u << 5,
    Indirect = 1u << 6,
    DeviceAddress = 1u << 7,
};

constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) noexcept {
    return static_cast<BufferUsage>(static_cast<u32>(left) | static_cast<u32>(right));
}

constexpr bool HasUsage(BufferUsage flags, BufferUsage usage) noexcept {
    return (static_cast<u32>(flags) & static_cast<u32>(usage)) != 0;
}

constexpr BufferUsage ReadFlags = BufferUsage::TransferSource | BufferUsage::Uniform |
                                  BufferUsage::Index | BufferUsage::Vertex | BufferUsage::Indirect;

constexpr BufferUsage AllFlags =
    ReadFlags | BufferUsage::TransferDestination | BufferUsage::Storage;

struct BufferDesc {
    MemoryUsage memory_usage;
    VAddr guest_address;
    BufferUsage usage;
    u64 size_bytes;
};

} // namespace VideoCore
