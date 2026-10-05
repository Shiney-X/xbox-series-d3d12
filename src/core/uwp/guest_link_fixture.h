// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_link_manifest.h"
#include "core/uwp/guest_loader_fixture.h"

namespace Core::Uwp {
[[nodiscard]] inline std::vector<std::uint8_t> MakeGuestLinkFixture(bool self = true) {
    const auto original = MakeGuestLoaderFixture();
    std::vector<std::uint8_t> elf(0xa00, 0);
    std::copy_n(original.begin(), 64, elf.begin());
    const auto put = [](std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value,
                        unsigned count = 8) {
        for (unsigned i = 0; i < count; ++i)
            bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    put(elf, 56, 5, 2);
    const auto ph = [&](unsigned index, std::uint32_t type, std::uint32_t flags,
                        std::uint64_t offset, std::uint64_t address, std::uint64_t size,
                        std::uint64_t memory, std::uint64_t alignment) {
        const auto start = 64 + index * 56;
        put(elf, start, type, 4);
        put(elf, start + 4, flags, 4);
        put(elf, start + 8, offset);
        put(elf, start + 16, address);
        put(elf, start + 32, size);
        put(elf, start + 40, memory);
        put(elf, start + 48, alignment);
    };
    ph(0, 1, 5, 0x400, 0x400100, 4, 4, 0x100);
    ph(1, 1, 6, 0x500, 0x404200, 4, 16, 0x100);
    ph(2, 0x61000000, 4, 0x600, 0, 0x400, 0, 16);
    ph(3, 2, 6, 0x800, 0, 240, 240, 8); // Dynamic table inside DYNLIBDATA.
    ph(4, 7, 4, 0, 0x404208, 0, 8, 8);  // Authored BSS-only TLS metadata.
    std::copy_n(original.begin() + 0x100, 4, elf.begin() + 0x400);
    std::copy_n(original.begin() + 0x200, 4, elf.begin() + 0x500);
    const auto text = [&](std::size_t offset, std::string_view value) {
        std::copy(value.begin(), value.end(), elf.begin() + 0x600 + offset);
    };
    text(1, "libFixture.prx");
    text(16, "libFixture");
    text(32, "fixture#A#B");
    text(48, "object#A#B");
    put(elf, 0x680 + 24, 32, 4);
    elf[0x680 + 24 + 4] = 0x12; // Undefined global function.
    put(elf, 0x680 + 48, 48, 4);
    elf[0x680 + 48 + 4] = 0x21; // Undefined weak object.
    put(elf, 0x700, 0x404208);
    put(elf, 0x708, 8); // RELATIVE, symbol zero.
    put(elf, 0x710, 0x400100);
    put(elf, 0x718, 0x404200);
    put(elf, 0x720, (1ULL << 32) | 7); // JUMP_SLOT, function symbol 1.
    constexpr std::array<std::pair<std::uint64_t, std::uint64_t>, 14> tags{
        {{0x61000035, 0},
         {0x61000037, 64},
         {0x61000039, 0x80},
         {0x6100003f, 72},
         {0x6100003b, 24},
         {0x6100002f, 0x100},
         {0x61000031, 24},
         {0x61000033, 24},
         {0x61000029, 0x118},
         {0x6100002d, 24},
         {0x6100002b, 7},
         {1, 1},
         {0x6100000f, 16 | (1ULL << 48)},
         {0x61000015, 16}}};
    for (std::size_t i = 0; i < tags.size(); ++i) {
        put(elf, 0x800 + i * 16, tags[i].first);
        put(elf, 0x808 + i * 16, tags[i].second);
    }
    if (!self)
        return elf;
    std::vector<std::uint8_t> file(0x1600, 0);
    put(file, 0, 0x1d3d154f, 4);
    file[5] = file[6] = file[8] = file[9] = 1;
    file[7] = 0x12;
    put(file, 12, 0x200, 2);
    put(file, 16, file.size(), 4);
    put(file, 24, 3, 2);
    for (unsigned i = 0; i < 3; ++i) {
        const auto index = i == 0 ? 2U : i - 1;
        const auto size = index == 2 ? 0x400U : 4U;
        put(file, 32 + i * 32, (std::uint64_t(index) << 20) | 0x800);
        put(file, 40 + i * 32, 0x1000 + index * 0x100);
        put(file, 48 + i * 32, size);
        put(file, 56 + i * 32, size);
    }
    std::copy_n(elf.begin(), 344, file.begin() + 128);
    std::copy_n(elf.begin() + 0x400, 4, file.begin() + 0x1000);
    std::copy_n(elf.begin() + 0x500, 4, file.begin() + 0x1100);
    std::copy_n(elf.begin() + 0x600, 0x400, file.begin() + 0x1200);
    return file;
}
[[nodiscard]] inline bool VerifyGuestLinkFixture(const GuestLinkManifest& manifest) {
    return manifest.valid && manifest.dynamic_entries == 14 && manifest.symbols == 3 &&
           manifest.undefined_functions == 1 && manifest.undefined_objects == 1 &&
           manifest.relocations == 2 && manifest.relative == 1 && manifest.jump_slots == 1 &&
           manifest.target_checks == 2 && manifest.tls_segments == 1 &&
           manifest.tls_memory_bytes == 8 && manifest.tls_file_bytes == 0 &&
           manifest.needed == std::vector<std::string>{"libFixture.prx"} &&
           manifest.modules == std::vector<std::string>{"libFixture"} &&
           manifest.libraries == std::vector<std::string>{"libFixture"} &&
           manifest.imports == std::vector<std::string>{"fixture#A#B", "object#A#B"};
}
} // namespace Core::Uwp
