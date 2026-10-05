// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstring>
#include "core/uwp/guest_import_resolver.h"

namespace Core::Uwp {
enum class GuestMemoryService { Copy, Set, Compare };
inline constexpr std::array<std::string_view, 3> GuestMemoryNids{"Q3VBxCXhUHs", "8zTFvBIAIN8",
                                                                 "DfivPArhucg"};
[[nodiscard]] inline GuestImportKey GuestMemoryKey(unsigned service) {
    if (service >= GuestMemoryNids.size())
        return {};
    return {std::string(GuestMemoryNids[service]),
            "libSceLibcInternal",
            "libSceLibcInternal",
            1,
            1,
            1,
            2};
}

// Immutable, host-owned descriptors. Only the harness may authorize backing.
// These are actual mapped host addresses, not synthetic ELF virtual addresses.
struct GuestMemoryRange {
    std::span<std::uint8_t> bytes;
    bool writable{};
};
struct GuestMemoryResult {
    bool valid{};
    std::uint64_t value{};
};
class GuestMemory {
public:
    explicit GuestMemory(std::span<const GuestMemoryRange> ranges) : ranges(ranges) {}

    [[nodiscard]] bool Valid() const noexcept {
        if (ranges.empty() || ranges.size() > 64)
            return false;
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            const auto begin = Address(ranges[i]);
            const auto size = ranges[i].bytes.size();
            if (!begin || !size || size > UINT64_MAX - begin)
                return false;
            for (std::size_t j = 0; j < i; ++j) {
                const auto other = Address(ranges[j]);
                if (begin < other + ranges[j].bytes.size() && other < begin + size)
                    return false;
            }
        }
        return true;
    }

    [[nodiscard]] GuestMemoryResult Invoke(GuestMemoryService service, std::uint64_t first,
                                           std::uint64_t second,
                                           std::uint64_t count) const noexcept {
        if (!Valid() || count > MaxTransfer ||
            (service != GuestMemoryService::Copy && service != GuestMemoryService::Set &&
             service != GuestMemoryService::Compare))
            return {};
        auto* destination = Find(first, count, service != GuestMemoryService::Compare);
        auto* source = service == GuestMemoryService::Set ? nullptr : Find(second, count, false);
        if (!destination || (service != GuestMemoryService::Set && !source))
            return {};
        // Reject overlapping memcpy, including identical pointers with nonzero
        // count. The C API does not define that case; this is not memmove.
        if (service == GuestMemoryService::Copy && count && first < second + count &&
            second < first + count)
            return {};
        if (!count)
            return {true, service == GuestMemoryService::Compare ? 0 : first};
        const auto size = static_cast<std::size_t>(count);
        if (service == GuestMemoryService::Set) {
            std::memset(destination, static_cast<unsigned char>(second), size);
            return {true, first};
        }
        if (service == GuestMemoryService::Copy) {
            std::memcpy(destination, source, size);
            return {true, first};
        }
        const auto compared = std::memcmp(destination, source, size);
        // Only the sign is specified by memcmp. Encode its signed s32 return
        // as a sign-extended register value, without a platform-specific magnitude.
        const std::int64_t sign = compared < 0 ? -1 : compared > 0 ? 1 : 0;
        return {true, static_cast<std::uint64_t>(sign)};
    }
    static constexpr std::uint64_t MaxTransfer = 1024 * 1024;

private:
    [[nodiscard]] static std::uint64_t Address(const GuestMemoryRange& range) noexcept {
        return reinterpret_cast<std::uintptr_t>(range.bytes.data());
    }
    [[nodiscard]] std::uint8_t* Find(std::uint64_t pointer, std::uint64_t count,
                                     bool write) const noexcept {
        for (const auto& range : ranges) {
            const auto begin = Address(range);
            if (pointer >= begin && pointer - begin < range.bytes.size()) {
                if ((write && !range.writable) || count > range.bytes.size() - (pointer - begin))
                    return nullptr;
                return range.bytes.data() + static_cast<std::size_t>(pointer - begin);
            }
        }
        // Zero length still requires a non-null authorized pointer. Prefer a
        // containing range above, so an adjacent readonly start cannot inherit
        // its writable neighbour's one-past permission. Never stitch ranges.
        if (!count)
            for (const auto& range : ranges)
                if ((!write || range.writable) && pointer == Address(range) + range.bytes.size())
                    return range.bytes.data() + range.bytes.size();
        return nullptr;
    }
    std::span<const GuestMemoryRange> ranges;
};
} // namespace Core::Uwp
