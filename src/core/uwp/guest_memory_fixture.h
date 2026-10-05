// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_hle_import_fixture.h"
#include "core/uwp/guest_memory.h"

namespace Core::Uwp {
[[nodiscard]] inline std::vector<std::uint8_t> MakeGuestMemoryFixture(bool self) {
    auto file = MakeGuestHleImportFixture(self);
    const auto offset = [&](std::size_t raw) {
        if (!self)
            return raw;
        if (raw >= 0x600)
            return 0x1200 + raw - 0x600;
        if (raw >= 0x500)
            return 0x1100 + raw - 0x500;
        if (raw >= 0x400)
            return 0x1000 + raw - 0x400;
        return 128 + raw;
    };
    const auto put = [&](std::size_t raw, std::uint64_t value, unsigned bytes = 8) {
        for (unsigned i = 0; i < bytes; ++i)
            file[offset(raw) + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    const auto text = [&](std::size_t raw, const std::string& value) {
        std::copy(value.begin(), value.end(), file.begin() + offset(raw));
    };
    std::fill_n(file.begin() + offset(0x600), 128 + 96, std::uint8_t{0});
    text(0x601, "libSceLibcInternal.prx");
    text(0x620, "libSceLibcInternal");
    put(64 + 56 + 40, 0x400); // Data LOAD incl. bounded scratch BSS, not the full RW page.
    put(64 + 4 * 56 + 16, 0);
    put(64 + 4 * 56 + 40, 0);
    put(64 + 4 * 56 + 48, 1);
    put(0x808 + 1 * 16, 128);
    put(0x808 + 3 * 16, 96);
    put(0x808 + 9 * 16, 72);
    put(0x808 + 12 * 16, 32 | (1ULL << 32) | (1ULL << 40) | (1ULL << 48));
    put(0x808 + 13 * 16, 32 | (1ULL << 32));
    put(0x700, 0x404218);
    for (unsigned i = 0; i < 3; ++i) {
        text(0x640 + i * 16, std::string(GuestMemoryNids[i]) + "#A#B");
        put(0x680 + (i + 1) * 24, 64 + i * 16, 4);
        put(0x680 + (i + 1) * 24 + 4, 0x12, 1);
        put(0x718 + i * 24, 0x404200 + i * 8);
        put(0x720 + i * 24, (std::uint64_t(i + 1) << 32) | 7);
        put(0x728 + i * 24, 0);
        // Two input pointers/values in RDI/RSI; set the third SysV arg RDX.
        // Positive entries 140/150/160 use 16 bytes; zero entries 180/190/1a0.
        for (unsigned zero = 0; zero < 2; ++zero) {
            const std::size_t entry = 0x140 + i * 16 + zero * 64;
            put(0x400 + entry - 0x100, 0xba, 1); // mov edx, immediate
            put(0x401 + entry - 0x100, zero ? 0 : 16, 4);
            put(0x405 + entry - 0x100, 0x25ff, 2);
            put(0x407 + entry - 0x100, 0x4200 + i * 8 - (entry + 11), 4);
        }
    }
    // Oversize/overflow rejection through the same real memcpy import.
    put(0x470, 0xba48, 2); // mov rdx, UINT64_MAX
    put(0x472, UINT64_MAX);
    put(0x47a, 0x25ff, 2);
    put(0x47c, 0x4200 - (0x170 + 16), 4);
    return file;
}

[[nodiscard]] inline FixtureBridge MakeGuestMemoryThunk(std::uint64_t callback,
                                                        std::uint64_t context) {
    FixtureBridge thunk;
    // Preserve third SysV argument before moving RSI into Win64 RDX.
    thunk.code = {0x48, 0x83, 0xec, 0x28, 0x49, 0x89, 0xd0, 0x48,
                  0x89, 0xf2, 0x48, 0x89, 0xf9, 0x49, 0xb9}; // hidden fourth argument R9
    for (unsigned i = 0; i < 8; ++i)
        thunk.code.push_back(static_cast<std::uint8_t>(context >> (8 * i)));
    thunk.code.insert(thunk.code.end(), {0x48, 0xb8});
    for (unsigned i = 0; i < 8; ++i)
        thunk.code.push_back(static_cast<std::uint8_t>(callback >> (8 * i)));
    thunk.code.insert(thunk.code.end(), {0xfc, 0xff, 0xd0});
    thunk.epilogue_offset = thunk.code.size();
    thunk.code.insert(thunk.code.end(), {0x48, 0x83, 0xc4, 0x28, 0xc3});
    thunk.unwind = {1, 4, 1, 0, 4, 0x42, 0, 0};
    return thunk;
}
} // namespace Core::Uwp
