// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <limits>
#include <vector>
#include "core/uwp/guest_preflight.h"

namespace Core::Uwp {
inline constexpr std::uint64_t GuestImageLimit = 16 * 1024 * 1024;
inline constexpr std::uint64_t GuestPageSize = 16384;

struct GuestLoadSegment {
    std::uint64_t file_offset{}, address{}, file_size{}, memory_size{};
    std::uint32_t flags{};
    std::uint32_t program_index{};
};

struct GuestLoadPlan {
    bool valid{};
    bool raw_elf{};
    std::uint64_t base{}, image_size{}, entry_offset{};
    std::vector<GuestLoadSegment> segments;
    std::string error;

    [[nodiscard]] std::string Details() const {
        return "stage=segment_plan;loader_linked=0;guest_executed=0;game_frame=0;plan_valid=" +
               std::to_string(valid) + ";raw_elf=" + std::to_string(raw_elf) +
               ";load_segments=" + std::to_string(segments.size()) +
               ";image_bytes=" + std::to_string(image_size) +
               ";guest_base=" + std::to_string(base) +
               ";entry_offset=" + std::to_string(entry_offset) +
               ";host_permissions_applied=0;error=" + error;
    }
};

// Deliberately restricted policy, not a replacement for the upstream loader.
// SELF p_offset is an ELF logical offset, not a container file offset.
[[nodiscard]] inline GuestLoadPlan PlanGuestLoads(std::span<const std::uint8_t> prefix,
                                                  std::uint64_t file_size) {
    GuestLoadPlan plan;
    const auto fail = [&](const char* error) {
        plan.error = error;
        return plan;
    };
    const auto header = InspectGuestPrefix(prefix, file_size);
    if (!header.header_valid)
        return fail("header_invalid");
    plan.raw_elf = !header.self_container;
    const auto read = [&](std::size_t offset, unsigned count) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= std::uint64_t(prefix[offset + i]) << (8 * i);
        return value;
    };
    const auto phoff = read(static_cast<std::size_t>(header.elf_offset) + 32, 8);
    if (header.program_headers > 128)
        return fail("program_header_limit");
    const auto available = prefix.size() - header.elf_offset;
    if (phoff > available || std::uint64_t(header.program_headers) * 56 > available - phoff)
        return fail("program_headers_outside_prefix");
    auto low = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t high = 0;
    bool executable_entry = false;
    for (std::size_t i = 0; i < header.program_headers; ++i) {
        const auto offset = static_cast<std::size_t>(header.elf_offset + phoff + i * 56);
        if (read(offset, 4) != 1) // PT_LOAD only; no TLS/dynamic/relocations yet.
            continue;
        const GuestLoadSegment segment{read(offset + 8, 8),
                                       read(offset + 16, 8),
                                       read(offset + 32, 8),
                                       read(offset + 40, 8),
                                       static_cast<std::uint32_t>(read(offset + 4, 4)),
                                       static_cast<std::uint32_t>(i)};
        const auto alignment = read(offset + 48, 8);
        if ((segment.flags & ~7U) != 0 || (segment.flags & 3U) == 3U)
            return fail("unsupported_segment_permissions"); // Never accept W+X.
        if (segment.file_size > segment.memory_size)
            return fail("file_size_exceeds_memory");
        if (alignment > 1 && ((alignment & (alignment - 1)) != 0 ||
                              segment.address % alignment != segment.file_offset % alignment))
            return fail("invalid_segment_alignment");
        if (segment.memory_size == 0) {
            continue;
        }
        if (segment.memory_size > GuestImageLimit ||
            segment.address > std::numeric_limits<std::uint64_t>::max() - segment.memory_size)
            return fail("segment_address_overflow_or_budget");
        // Container payload ranges require a separate SELF block adapter.
        if (plan.raw_elf && (segment.file_offset > file_size ||
                             segment.file_size > file_size - segment.file_offset))
            return fail("segment_outside_file");
        const auto end = segment.address + segment.memory_size;
        for (const auto& previous : plan.segments) {
            if (segment.address < previous.address + previous.memory_size && previous.address < end)
                return fail("overlapping_load_segments");
        }
        low = std::min(low, segment.address);
        high = std::max(high, end);
        executable_entry |= (segment.flags & 1U) && header.entry >= segment.address &&
                            header.entry - segment.address < segment.file_size;
        plan.segments.push_back(segment);
    }
    if (plan.segments.empty())
        return fail("no_load_segments");
    if (!executable_entry)
        return fail("entry_outside_file_backed_executable_segment");
    plan.base = low - low % GuestPageSize;
    const auto span = high - plan.base;
    if (span > GuestImageLimit)
        return fail("image_budget_exceeded");
    plan.image_size = (span + GuestPageSize - 1) / GuestPageSize * GuestPageSize;
    plan.entry_offset = header.entry - plan.base;
    plan.valid = true;
    return plan;
}

struct GuestStagedImage {
    GuestLoadPlan plan;
    std::vector<std::uint8_t> bytes;
    std::string error;
};

// Re-validate the immutable input internally: callers cannot supply a forged plan.
// Data allocation only, no fixed virtual address, host executable permission or call.
[[nodiscard]] inline GuestStagedImage StageRawGuest(std::span<const std::uint8_t> file) {
    GuestStagedImage image;
    image.plan = PlanGuestLoads(file.first(std::min<std::size_t>(file.size(), GuestPrefixLimit)),
                                file.size());
    if (!image.plan.valid) {
        image.error = image.plan.error;
        return image;
    }
    if (!image.plan.raw_elf) {
        image.error = "self_payload_adapter_required";
        return image;
    }
    image.bytes.resize(static_cast<std::size_t>(image.plan.image_size), 0);
    for (const auto& segment : image.plan.segments) {
        std::copy_n(file.begin() + static_cast<std::size_t>(segment.file_offset),
                    static_cast<std::size_t>(segment.file_size),
                    image.bytes.begin() +
                        static_cast<std::size_t>(segment.address - image.plan.base));
    }
    return image;
}
} // namespace Core::Uwp
