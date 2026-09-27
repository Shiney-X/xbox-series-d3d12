// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>

#include "common/types.h"

namespace VideoCore {

// Semantic buffer accesses issued by the guest command stream. Native API
// access and pipeline-stage flags are selected by each renderer.
enum class BufferAccess : u8 {
    Initial,
    General,
    VertexRead,
    IndexRead,
    IndirectRead,
    ShaderRead,
    ShaderWrite,
    TransferRead,
    TransferWrite,
};

struct BufferTransition {
    BufferAccess before;
    BufferAccess after;
    u64 offset;
    u64 size;
};

class BufferSyncState {
public:
    [[nodiscard]] std::optional<BufferTransition> Transition(BufferAccess next, u64 offset,
                                                             u64 buffer_size) {
        if (current == next) {
            return std::nullopt;
        }

        if (offset >= buffer_size) {
            return std::nullopt;
        }
        const BufferTransition transition{current, next, offset, buffer_size - offset};
        current = next;
        return transition;
    }

private:
    BufferAccess current{BufferAccess::Initial};
};

} // namespace VideoCore
