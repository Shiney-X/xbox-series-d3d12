// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_loader_fixture.h"
#include "core/uwp/guest_payload.h"

namespace Core::Uwp {
// Authored SELF data fixture with reordered container blocks. The embedded ELF
// logical offsets intentionally do not point to its container payload bytes.
[[nodiscard]] inline std::vector<std::uint8_t> MakeGuestPayloadFixture() {
    const auto elf = MakeGuestLoaderFixture();
    std::vector<std::uint8_t> file(0x194, 0);
    const auto put = [&](std::size_t offset, std::uint64_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            file[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    put(0, 0x1d3d154f, 4);
    file[5] = file[6] = file[8] = file[9] = 1;
    file[7] = 0x12;
    put(12, 0x120, 2);
    put(16, file.size(), 4);
    put(24, 2, 2);
    put(32, (1ULL << 20) | 0x800, 8); // PT_LOAD index 1 before index 0.
    put(40, 0x190, 8);
    put(48, 4, 8);
    put(56, 4, 8);
    put(64, 0x800, 8);
    put(72, 0x180, 8);
    put(80, 4, 8);
    put(88, 4, 8);
    std::copy_n(elf.begin(), 176, file.begin() + 96);
    std::copy_n(elf.begin() + 0x100, 4, file.begin() + 0x180);
    std::copy_n(elf.begin() + 0x200, 4, file.begin() + 0x190);
    return file;
}
} // namespace Core::Uwp
