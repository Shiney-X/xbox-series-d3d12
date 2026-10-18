// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_link_fixture.h"
#include "core/uwp/native_entry_prefix.h"

namespace Core::Uwp {
// Authored CRT/PLT path; no game bytes, copyrighted modules or ROMs in tests.
[[nodiscard]] inline std::vector<std::uint8_t> MakeNativeEntryFixture(bool self) {
    auto elf = MakeGuestLinkFixture(false);
    const auto put = [](std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value,
                        unsigned width = 8) {
        for (unsigned i = 0; i < width; ++i)
            bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    put(elf, 64 + 32, 0x100);
    put(elf, 64 + 40, 0x100);
    std::fill(elf.begin() + 0x400, elf.begin() + 0x500, std::uint8_t{0xcc});
    std::copy(NativeCrtPrefix.begin(), NativeCrtPrefix.end(), elf.begin() + 0x400);
    elf[0x414] = 0xe8;
    put(elf, 0x415, 0x80 - 25, 4); // CALL first PLT, at guest 0x400180.
    elf[0x480] = 0xff;
    elf[0x481] = 0x25;
    put(elf, 0x482, 0x404200 - (0x400180 + 6), 4);
    if (!self)
        return elf;
    auto file = MakeGuestLinkFixture(true);
    put(file, 128 + 64 + 32, 0x100);
    put(file, 128 + 64 + 40, 0x100);
    // Block 1 is the executable program header 0, already at physical 0x1000.
    put(file, 32 + 32 + 16, 0x100);
    put(file, 32 + 32 + 24, 0x100);
    std::copy_n(elf.begin() + 0x400, 0x100, file.begin() + 0x1000);
    return file;
}
} // namespace Core::Uwp
