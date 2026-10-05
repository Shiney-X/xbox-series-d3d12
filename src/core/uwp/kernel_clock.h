// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include "core/uwp/guest_import_resolver.h"

namespace Core::Uwp {
enum class KernelClockService { Microseconds, Counter, Frequency };
inline constexpr std::array<const char*, 3> KernelClockNids{"4J2sUJmuHZQ", "fgxnMeTNUtY",
                                                            "BNowx2l588E"};

// Pointer-free u64(void) APIs from kernel/time.cpp. Deliberately no ReadTsc,
// GetTscFrequency or claim that a guest's direct RDTSC has this clock's units.
[[nodiscard]] inline GuestImportKey KernelClockKey(unsigned service) {
    if (service >= KernelClockNids.size())
        return {};
    return {KernelClockNids[service], "libkernel", "libkernel", 1, 1, 1, 2};
}
struct KernelClock {
    std::uint64_t frequency{}, epoch{};
    [[nodiscard]] bool Valid() const {
        return frequency > 0 && frequency <= UINT64_MAX / 1000000;
    }
    [[nodiscard]] bool Read(std::uint64_t now, KernelClockService service,
                            std::uint64_t& value) const noexcept {
        if (!Valid())
            return false;
        if (service == KernelClockService::Frequency) {
            value = frequency;
            return true;
        }
        if (now < epoch)
            return false;
        const auto ticks = now - epoch;
        if (service == KernelClockService::Counter) {
            value = ticks;
            return true;
        }
        if (service != KernelClockService::Microseconds)
            return false;
        const auto seconds = ticks / frequency;
        const auto fractional = (ticks % frequency) * 1000000 / frequency;
        if (seconds > (UINT64_MAX - fractional) / 1000000)
            return false;
        value = seconds * 1000000 + fractional;
        return true;
    }
};
} // namespace Core::Uwp
