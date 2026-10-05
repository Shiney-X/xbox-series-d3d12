// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <limits>
#include "core/uwp/guest_execution_fixture.h"

namespace Core::Uwp {
inline constexpr std::size_t FixtureHleEntryOffset = 0x140;
inline constexpr std::size_t FixtureHlePointerOffset = 0x4200;
inline constexpr std::uint64_t FixtureHleUnsupported = 0xffffffffffffffffULL;
inline constexpr std::uint64_t FixtureHleOverflow = 0xfffffffffffffffeULL;
inline constexpr std::uint64_t FixtureHleMissingContext = 0xfffffffffffffffdULL;

struct FixtureThreadContext {
    std::uint64_t owner{}, value{}, calls{};
};

[[nodiscard]] inline std::uint64_t DispatchFixtureHle(std::uint64_t operation,
                                                      std::uint64_t argument) noexcept;

// Host TLS stores this authored context. This is not the upstream Tcb, a DTV,
// GS-relative guest TLS, errno, or an Orbis pthread implementation.
[[nodiscard]] inline std::uint64_t DispatchFixtureContext(FixtureThreadContext& context,
                                                          std::uint64_t operation,
                                                          std::uint64_t argument) noexcept {
    ++context.calls;
    if (operation == 2) {
        context.value = argument;
        return context.value;
    }
    if (operation == 3 && argument == 0)
        return context.value;
    if (operation == 4 && argument == 0)
        return context.owner;
    return DispatchFixtureHle(operation, argument);
}

// Authored test service, not an Orbis syscall/NID resolver. No guest pointers,
// file access, exceptions, global state or externally selected host addresses.
[[nodiscard]] inline std::uint64_t DispatchFixtureHle(std::uint64_t operation,
                                                      std::uint64_t argument) noexcept {
    if (operation != 1)
        return FixtureHleUnsupported;
    if (argument == std::numeric_limits<std::uint64_t>::max())
        return FixtureHleOverflow;
    return argument + 1;
}

[[nodiscard]] inline std::vector<std::uint8_t> MakeGuestHleFixture() {
    auto file = MakeGuestExecutionFixture();
    // Leaf tail-call: jmp qword ptr [rip+disp32]. Pointer is patched by the
    // fixture harness in its own RW data page, never resolved from a game file.
    const auto displacement =
        static_cast<std::uint32_t>(FixtureHlePointerOffset - (FixtureHleEntryOffset + 6));
    file[FixtureHleEntryOffset] = 0xff;
    file[FixtureHleEntryOffset + 1] = 0x25;
    for (unsigned i = 0; i < 4; ++i)
        file[FixtureHleEntryOffset + 2 + i] = static_cast<std::uint8_t>(displacement >> (8 * i));
    return file;
}

// Two integer arguments only: SysV RDI/RSI -> Win64 RCX/RDX. A 40-byte frame
// supplies 32-byte shadow space and call alignment. Win64 preserves every SysV
// nonvolatile GPR; no floating-point, varargs, stack arguments or aggregates.
[[nodiscard]] inline FixtureBridge MakeFixtureHleThunk(std::uint64_t callback,
                                                       std::uint64_t context_key = 0) {
    FixtureBridge thunk;
    thunk.code = {0x48, 0x83, 0xec, 0x28, // sub rsp,40
                  0x48, 0x89, 0xf9,       // mov rcx,rdi
                  0x48, 0x89, 0xf2,       // mov rdx,rsi
                  0xfc,                   // cld
                  0x48, 0xb8};            // movabs rax,callback
    for (unsigned i = 0; i < 8; ++i)
        thunk.code.push_back(static_cast<std::uint8_t>(callback >> (8 * i)));
    // Hidden third Win64 argument: opaque, harness-owned host TLS key.
    // Not supplied by the guest, and not returned as a guest address.
    thunk.code.insert(thunk.code.end(), {0x49, 0xb8}); // movabs r8,context_key
    for (unsigned i = 0; i < 8; ++i)
        thunk.code.push_back(static_cast<std::uint8_t>(context_key >> (8 * i)));
    thunk.code.insert(thunk.code.end(), {0xff, 0xd0}); // call rax
    thunk.epilogue_offset = thunk.code.size();
    thunk.code.insert(thunk.code.end(), {0x48, 0x83, 0xc4, 0x28, 0xc3});
    // Version 1; one UWOP_ALLOC_SMALL (40 bytes), padded to DWORD alignment.
    thunk.unwind = {1, 4, 1, 0, 4, 0x42, 0, 0};
    return thunk;
}
} // namespace Core::Uwp
