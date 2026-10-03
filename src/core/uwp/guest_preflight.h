// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace Core::Uwp {
inline constexpr std::uint32_t GuestPrefixLimit = 16384;

struct GuestPreflight {
    bool header_valid{};
    bool self_container{};
    bool orbis_identity{};
    bool encrypted_segments{};
    bool compressed_segments{};
    std::uint16_t type{};
    std::uint16_t program_headers{};
    std::uint64_t entry{};
    std::uint64_t elf_offset{};
    std::string error;

    [[nodiscard]] std::string Details() const {
        return "stage=header_inspection;loader_linked=0;guest_executed=0;game_frame=0;container=" +
               std::string(self_container ? "SELF" : "ELF") +
               ";header_valid=" + std::to_string(header_valid) +
               ";orbis_identity=" + std::to_string(orbis_identity) +
               ";encrypted_segments=" + std::to_string(encrypted_segments) +
               ";compressed_segments=" + std::to_string(compressed_segments) +
               ";elf_offset=" + std::to_string(elf_offset) + ";elf_type=" + std::to_string(type) +
               ";entry=" + std::to_string(entry) +
               ";program_headers=" + std::to_string(program_headers) + ";error=" + error;
    }
};

// Bounded, read-only preflight using the upstream ELF/SELF header layout.
// Not Core::Loader::Elf, not full segment validation/decryption or a loader.
[[nodiscard]] inline GuestPreflight InspectGuestPrefix(std::span<const std::uint8_t> bytes,
                                                       std::uint64_t file_size) {
    GuestPreflight result;
    const auto fail = [&](const char* error) {
        result.error = error;
        return result;
    };
    if (bytes.size() > GuestPrefixLimit || bytes.size() > file_size || bytes.size() < 4)
        return fail("invalid_prefix_size");
    const auto read = [&](std::size_t offset, unsigned count) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= std::uint64_t(bytes[offset + i]) << (8 * i);
        return value;
    };
    result.self_container = read(0, 4) == 0x1d3d154f;
    if (result.self_container) {
        if (bytes.size() < 32)
            return fail("truncated_self_header");
        if (bytes[4] != 0 || bytes[5] != 1 || bytes[6] != 1 || bytes[7] != 0x12 || bytes[8] != 1 ||
            bytes[9] != 1)
            return fail("unsupported_self_identity");
        const auto segments = read(24, 2);
        result.elf_offset = 32 + segments * 32;
        if (result.elf_offset > bytes.size() || bytes.size() - result.elf_offset < 64)
            return fail("self_elf_outside_prefix");
        for (std::size_t i = 0; i < segments; ++i) {
            const auto flags = read(32 + i * 32, 8);
            result.encrypted_segments |= (flags & 2) != 0;
            result.compressed_segments |= (flags & 8) != 0;
        }
    }
    const auto offset = static_cast<std::size_t>(result.elf_offset);
    if (bytes.size() - offset < 64)
        return fail("truncated_elf_header");
    if (read(offset, 4) != 0x464c457f)
        return fail("invalid_elf_magic");
    if (bytes[offset + 4] != 2 || bytes[offset + 5] != 1 || bytes[offset + 6] != 1 ||
        read(offset + 18, 2) != 62 || read(offset + 20, 4) != 1)
        return fail("requires_elf64_little_endian_x86_64");
    if (read(offset + 52, 2) != 64)
        return fail("invalid_elf_header_size");
    result.type = static_cast<std::uint16_t>(read(offset + 16, 2));
    if (result.type != 2 && result.type != 3 && result.type != 0xfe00 && result.type != 0xfe10 &&
        result.type != 0xfe18)
        return fail("unsupported_elf_type");
    result.entry = read(offset + 24, 8);
    result.program_headers = static_cast<std::uint16_t>(read(offset + 56, 2));
    if (!result.program_headers || result.program_headers == 0xffff || read(offset + 54, 2) != 56)
        return fail("unsupported_program_header_layout");
    const auto phoff = read(offset + 32, 8);
    const auto available = file_size - result.elf_offset;
    if (phoff < 64 || phoff > available ||
        std::uint64_t(result.program_headers) * 56 > available - phoff)
        return fail("program_header_table_outside_file");
    result.orbis_identity =
        bytes[offset + 7] == 9 && bytes[offset + 8] == 0 && result.type >= 0xfe00;
    result.header_valid = true;
    return result;
}
} // namespace Core::Uwp
