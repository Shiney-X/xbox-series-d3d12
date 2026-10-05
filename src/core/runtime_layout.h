// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include "common/types.h"

namespace Libraries::Fiber {
struct OrbisFiberContext;
}
namespace Core {
// Shared upstream startup/TLS ABI layouts; no host APIs or execution policy.
struct EntryParams {
    int argc;
    u32 padding;
    const char* argv[33];
    VAddr entry_addr;
};
union DtvEntry {
    std::size_t counter;
    u8* pointer;
};
struct Tcb {
    Tcb* tcb_self;
    DtvEntry* tcb_dtv;
    void* tcb_thread;
    void* tcb_spare[2];
    u64 tcb_canary;
    ::Libraries::Fiber::OrbisFiberContext* tcb_fiber;
};
// Orbis x64 ABI. This header also compiles with MSVC without sysv attributes.
static_assert(sizeof(void*) == 8 && sizeof(EntryParams) == 280 && sizeof(Tcb) == 56);
static_assert(offsetof(EntryParams, argv) == 8 && offsetof(EntryParams, entry_addr) == 272);
static_assert(sizeof(DtvEntry) == 8 && offsetof(Tcb, tcb_canary) == 40);
} // namespace Core
