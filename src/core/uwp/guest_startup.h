// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstring>
#include "core/runtime_layout.h"
#include "core/uwp/guest_data_link.h"

namespace Core::Uwp {
inline constexpr std::size_t GuestStartupStackSize = 128 * 1024;
struct GuestStartupLayout {
    bool valid{};
    std::uint64_t params{}, initial_rsp{}, tcb{}, dtv{}, tls{};
    std::string error;
};
// Builds data only. Does not change RSP, bind FS/GS, start a thread or call entry.
// Caller owns fixed backing and must keep it alive while these addresses exist.
[[nodiscard]] inline GuestStartupLayout PrepareGuestStartup(
    std::span<std::uint8_t> stack, std::span<std::uint8_t> tls_backing,
    std::span<const std::uint8_t> tls_initial, std::uint64_t tls_memory,
    std::uint64_t tls_alignment, std::uint64_t entry, std::string_view argv0) {
    GuestStartupLayout result;
    const auto fail = [&](const char* error) {
        result.error = error;
        return result;
    };
    const auto stack_base = reinterpret_cast<std::uintptr_t>(stack.data());
    const auto tls_base = reinterpret_cast<std::uintptr_t>(tls_backing.data());
    if (stack.size() != GuestStartupStackSize || !stack_base || stack_base % 16 ||
        stack.size() > UINT64_MAX - stack_base || !tls_base || tls_base % 32 ||
        tls_backing.size() > UINT64_MAX - tls_base || !entry || argv0.empty() ||
        argv0.size() > 256 || argv0.find('\0') != std::string_view::npos ||
        tls_memory > 16 * 1024 * 1024 || tls_initial.size() > tls_memory || !tls_alignment ||
        tls_alignment > 32 || (tls_alignment & (tls_alignment - 1)))
        return fail("startup_layout_or_budget_invalid");
    const auto tls_aligned = (static_cast<std::size_t>(tls_memory) + 31) & ~std::size_t{31};
    if (tls_aligned + 64 + 3 * sizeof(Core::DtvEntry) > tls_backing.size())
        return fail("tls_backing_too_small");
    // All validation precedes any writes; inputs must not alias either output.
    const auto overlaps = [](std::uint64_t a, std::uint64_t size, std::uint64_t b,
                             std::uint64_t other_size) {
        return size && other_size && a < b + other_size && b < a + size;
    };
    const auto input = reinterpret_cast<std::uintptr_t>(tls_initial.data());
    const auto argument = reinterpret_cast<std::uintptr_t>(argv0.data());
    if (tls_initial.size() > UINT64_MAX - input || argv0.size() > UINT64_MAX - argument ||
        overlaps(stack_base, stack.size(), tls_base, tls_backing.size()) ||
        overlaps(input, tls_initial.size(), stack_base, stack.size()) ||
        overlaps(input, tls_initial.size(), tls_base, tls_backing.size()) ||
        overlaps(argument, argv0.size(), stack_base, stack.size()) ||
        overlaps(argument, argv0.size(), tls_base, tls_backing.size()))
        return fail("startup_input_output_overlap");
    std::fill(stack.begin(), stack.end(), std::uint8_t{0});
    std::fill(tls_backing.begin(), tls_backing.end(), std::uint8_t{0});
    if (!tls_initial.empty())
        std::memcpy(tls_backing.data(), tls_initial.data(), tls_initial.size());
    result.params = stack_base + 0x100;
    result.initial_rsp = stack_base + stack.size() - 24; // Upstream RunMainEntry: %16 == 8.
    result.tls = tls_base;
    result.tcb = tls_base + tls_aligned;
    result.dtv = result.tcb + 64;
    Core::EntryParams params{};
    params.argc = 1;
    params.argv[0] = reinterpret_cast<const char*>(stack_base + 0x400);
    params.entry_addr = static_cast<VAddr>(entry);
    std::memcpy(stack.data() + 0x100, &params, sizeof(params));
    std::memcpy(stack.data() + 0x400, argv0.data(), argv0.size());
    // Matches the two qwords pushed by upstream; not a synthetic return address.
    std::memcpy(stack.data() + stack.size() - 24, &params, 16);
    Core::Tcb tcb{};
    tcb.tcb_self = reinterpret_cast<Core::Tcb*>(result.tcb);
    tcb.tcb_dtv = reinterpret_cast<Core::DtvEntry*>(result.dtv);
    // No pthread structure, libc allocator or canary claim until runtime binds.
    std::memcpy(tls_backing.data() + tls_aligned, &tcb, sizeof(tcb));
    std::array<Core::DtvEntry, 3> dtv{};
    dtv[0].counter = 1; // Single-module generation.
    dtv[1].counter = 1; // Single-module max TLS index.
    dtv[2].pointer = tls_memory ? reinterpret_cast<u8*>(tls_base) : nullptr;
    std::memcpy(tls_backing.data() + tls_aligned + 64, dtv.data(), sizeof(dtv));
    result.valid = true;
    return result;
}

// Conservative logical page plan. Execution remains disabled in preparation.
[[nodiscard]] inline std::vector<std::uint8_t> StartupPageFlags(const GuestLoadPlan& plan) {
    if (!plan.valid || !plan.image_size || plan.image_size > GuestImageLimit ||
        plan.image_size % 4096 || plan.base > UINT64_MAX - plan.image_size)
        return {};
    std::vector<std::uint8_t> pages(static_cast<std::size_t>(plan.image_size / 4096));
    for (const auto& segment : plan.segments) {
        if (!segment.memory_size || segment.address < plan.base || (segment.flags & ~7U) ||
            !(segment.flags & 4U) || segment.address - plan.base > plan.image_size ||
            segment.memory_size > plan.image_size - (segment.address - plan.base))
            return {};
        const auto offset = segment.address - plan.base;
        const auto first = offset / 4096;
        const auto last = (offset + segment.memory_size - 1) / 4096;
        for (auto page = first; page <= last; ++page) {
            pages[static_cast<std::size_t>(page)] |= static_cast<std::uint8_t>(segment.flags);
            if ((pages[static_cast<std::size_t>(page)] & 3) == 3)
                return {}; // Reject page-rounded W/X overlap, not only segment W+X.
        }
    }
    return pages;
}
} // namespace Core::Uwp
