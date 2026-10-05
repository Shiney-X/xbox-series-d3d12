// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_load_plan.h"

namespace Core::Uwp {
inline constexpr std::uint64_t GuestPayloadFileLimit = 32 * 1024 * 1024;

struct GuestPayload {
    GuestStagedImage image;
    std::uint64_t copied_bytes{}, bss_bytes{}, checksum{};
    bool bss_verified{};

    [[nodiscard]] bool Ready() const {
        return image.plan.valid && image.error.empty() && !image.bytes.empty() && bss_verified;
    }
    [[nodiscard]] std::string Details() const {
        const auto relro = std::count_if(
            image.plan.segments.begin(), image.plan.segments.end(),
            [](const GuestLoadSegment& segment) { return segment.type == 0x61000010; });
        return "stage=payload_data_staging;loader_linked=0;guest_executed=0;game_frame=0;"
               "host_permissions_applied=0;data_staged=" +
               std::to_string(Ready()) + ";payload_loaded=" + std::to_string(Ready()) +
               ";container=" + (image.plan.raw_elf ? "ELF" : "SELF") + ";load_segments=" +
               std::to_string(image.plan.segments.size() - static_cast<std::size_t>(relro)) +
               ";relro_segments=" + std::to_string(relro) +
               ";image_bytes=" + std::to_string(image.bytes.size()) +
               ";copied_bytes=" + std::to_string(copied_bytes) +
               ";bss_bytes=" + std::to_string(bss_bytes) +
               ";bss_verified=" + std::to_string(bss_verified) +
               ";checksum_fnv1a64=" + std::to_string(checksum) + ";error=" + image.error;
    }
};

// Immutable bounded snapshot only. SELF offsets are resolved through blocked
// segment IDs (program header indices), never used as raw ELF file offsets.
// No decryption, decompression, relocation, import resolution or execution.
[[nodiscard]] inline GuestPayload StageGuestPayload(std::span<const std::uint8_t> file) {
    GuestPayload payload;
    const auto fail = [&](const char* error) {
        payload.image.error = error;
        payload.image.bytes.clear();
        return payload;
    };
    if (file.size() > GuestPayloadFileLimit)
        return fail("payload_file_budget_exceeded");
    const auto prefix = file.first(std::min<std::size_t>(file.size(), GuestPrefixLimit));
    payload.image.plan = PlanGuestLoads(prefix, file.size(), true);
    if (!payload.image.plan.valid)
        return fail(payload.image.plan.error.c_str());
    const auto read = [&](std::size_t offset, unsigned count) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= std::uint64_t(file[offset + i]) << (8 * i);
        return value;
    };
    const auto header = InspectGuestPrefix(prefix, file.size());
    std::vector<std::uint64_t> offsets;
    offsets.reserve(payload.image.plan.segments.size());
    if (payload.image.plan.raw_elf) {
        for (const auto& segment : payload.image.plan.segments)
            offsets.push_back(segment.file_offset);
    } else {
        if (header.encrypted_segments || header.compressed_segments)
            return fail("encrypted_or_compressed_self_unsupported");
        const auto phoff = read(static_cast<std::size_t>(header.elf_offset) + 32, 8);
        const auto table_end =
            header.elf_offset + phoff + std::uint64_t(header.program_headers) * 56;
        const auto metadata_end = read(12, 2) + read(14, 2);
        if (metadata_end < table_end || metadata_end > file.size())
            return fail("invalid_self_metadata_range");
        struct Block {
            std::uint64_t offset{}, size{};
            bool present{};
        };
        std::vector<Block> blocks(header.program_headers);
        std::vector<Block> ranges;
        for (std::size_t i = 0; i < read(24, 2); ++i) {
            const auto flags = read(32 + i * 32, 8);
            const auto offset = read(40 + i * 32, 8);
            const auto size = read(48 + i * 32, 8);
            const auto memory_size = read(56 + i * 32, 8);
            if (offset < metadata_end || offset > file.size() || size > file.size() - offset)
                return fail("self_payload_outside_file");
            for (const auto& range : ranges)
                if (size && range.size && offset < range.offset + range.size &&
                    range.offset < offset + size)
                    return fail("overlapping_self_payloads");
            ranges.push_back({offset, size, true});
            if ((flags & 0x800) == 0)
                continue; // Auxiliary/signature blocks are not loadable segments.
            const auto index = static_cast<std::size_t>((flags >> 20) & 0xfff);
            if (index >= blocks.size())
                return fail("self_program_index_outside_table");
            if (blocks[index].present)
                return fail("duplicate_self_program_block");
            if (size != memory_size)
                return fail("unsupported_self_block_size");
            blocks[index] = {offset, size, true};
        }
        for (const auto& segment : payload.image.plan.segments) {
            if (segment.file_size == 0) {
                offsets.push_back(0); // BSS-only PT_LOAD requires no file payload.
                continue;
            }
            const auto& block = blocks[segment.program_index];
            if (!block.present)
                return fail("missing_self_load_block");
            if (block.size != segment.file_size)
                return fail("self_load_size_mismatch");
            offsets.push_back(block.offset);
        }
    }
    // All payload mappings validated before allocating/copying the image.
    payload.image.bytes.resize(static_cast<std::size_t>(payload.image.plan.image_size), 0);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const auto& segment = payload.image.plan.segments[i];
        const auto destination =
            static_cast<std::size_t>(segment.address - payload.image.plan.base);
        std::copy_n(file.begin() + static_cast<std::size_t>(offsets[i]),
                    static_cast<std::size_t>(segment.file_size),
                    payload.image.bytes.begin() + destination);
        payload.copied_bytes += segment.file_size;
        payload.bss_bytes += segment.memory_size - segment.file_size;
    }
    payload.bss_verified = true;
    for (const auto& segment : payload.image.plan.segments) {
        const auto start =
            static_cast<std::size_t>(segment.address - payload.image.plan.base + segment.file_size);
        const auto end = static_cast<std::size_t>(segment.address - payload.image.plan.base +
                                                  segment.memory_size);
        payload.bss_verified &=
            std::all_of(payload.image.bytes.begin() + start, payload.image.bytes.begin() + end,
                        [](std::uint8_t value) { return value == 0; });
    }
    // Diagnostic fingerprint, not a cryptographic integrity/security check.
    payload.checksum = 14695981039346656037ULL;
    for (const auto byte : payload.image.bytes)
        payload.checksum = (payload.checksum ^ byte) * 1099511628211ULL;
    return payload;
}
} // namespace Core::Uwp
