// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_load_plan.h"

namespace Core::Uwp {
// Authored ELF data fixture: two PT_LOADs, marker bytes and BSS, not game code.
[[nodiscard]] inline std::vector<std::uint8_t> MakeGuestLoaderFixture() {
    std::vector<std::uint8_t> file(0x204, 0);
    const auto put = [&](std::size_t offset, std::uint64_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            file[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    put(0, 0x464c457f, 4);
    file[4] = 2;
    file[5] = 1;
    file[6] = 1;
    file[7] = 9;
    put(16, 0xfe10, 2);
    put(18, 62, 2);
    put(20, 1, 4);
    put(24, 0x400100, 8);
    put(32, 64, 8);
    put(52, 64, 2);
    put(54, 56, 2);
    put(56, 2, 2);
    for (unsigned i = 0; i < 2; ++i) {
        const std::size_t offset = 64 + i * 56;
        put(offset, 1, 4);
        put(offset + 4, i == 0 ? 5 : 6, 4); // R+X and R+W guest metadata.
        put(offset + 8, i == 0 ? 0x100 : 0x200, 8);
        put(offset + 16, i == 0 ? 0x400100 : 0x404200, 8);
        put(offset + 32, 4, 8);
        put(offset + 40, i == 0 ? 4 : 16, 8);
        put(offset + 48, 0x100, 8);
    }
    file[0x100] = 0x11;
    file[0x101] = 0x22;
    file[0x102] = 0x33;
    file[0x103] = 0x44;
    file[0x200] = 0xaa;
    file[0x201] = 0xbb;
    file[0x202] = 0xcc;
    file[0x203] = 0xdd;
    return file;
}

[[nodiscard]] inline bool VerifyGuestLoaderFixture(const GuestStagedImage& image) {
    if (!image.error.empty() || !image.plan.valid || image.plan.base != 0x400000 ||
        image.plan.entry_offset != 0x100 || image.bytes.size() != 32768 ||
        image.plan.segments.size() != 2 || image.plan.segments[0].flags != 5 ||
        image.plan.segments[1].flags != 6)
        return false;
    const auto file = MakeGuestLoaderFixture();
    for (std::size_t i = 0; i < image.bytes.size(); ++i) {
        const auto expected = i >= 0x100 && i < 0x104     ? file[i]
                              : i >= 0x4200 && i < 0x4204 ? file[i - 0x4000]
                                                          : std::uint8_t{0};
        if (image.bytes[i] != expected)
            return false;
    }
    return true;
}
} // namespace Core::Uwp
