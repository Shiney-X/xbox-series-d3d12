// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_data_link.h"
#include "core/uwp/guest_link_fixture.h"

namespace Core::Uwp {
[[nodiscard]] inline bool VerifyGuestDataLinkFixtures() {
    const auto read = [](std::span<const std::uint8_t> bytes, std::size_t offset) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i)
            value |= std::uint64_t(bytes[offset + i]) << (i * 8);
        return value;
    };
    for (const bool self : {false, true}) {
        auto file = MakeGuestLinkFixture(self);
        const auto partial = StageGuestDataLink(file);
        if (!partial.valid || partial.complete || partial.relative_applied != 1 ||
            partial.local_applied != 0 || partial.pending_imports != 1 ||
            !partial.writes_verified || !partial.untouched_verified ||
            read(partial.payload.image.bytes, 0x4208) != GuestDiagnosticLoadBias + 0x400100 ||
            read(partial.payload.image.bytes, 0x4200) != 0xddccbbaa)
            return false;
        const auto strict = StageGuestDataLink(file, GuestDiagnosticLoadBias, true);
        if (!strict.payload.Ready() || strict.valid || strict.relative_applied != 0 ||
            strict.local_applied != 0 || strict.error != "pending_relocations_require_resolver" ||
            read(strict.payload.image.bytes, 0x4208) != 0 ||
            read(strict.payload.image.bytes, 0x4200) != 0xddccbbaa)
            return false;
        const auto symbol = self ? 0x1298U : 0x698U;
        file[symbol + 4] = 2; // STB_LOCAL, STT_FUNC.
        file[symbol + 6] = 1; // Defined section, not SHN_UNDEF.
        for (unsigned i = 0; i < 8; ++i)
            file[symbol + 8 + i] = static_cast<std::uint8_t>(0x400100ULL >> (i * 8));
        const auto local = StageGuestDataLink(file, GuestDiagnosticLoadBias, true);
        if (!local.valid || !local.complete || local.relative_applied != 1 ||
            local.local_applied != 1 || !local.writes_verified || !local.untouched_verified ||
            read(local.payload.image.bytes, 0x4200) != GuestDiagnosticLoadBias + 0x400100 ||
            read(local.payload.image.bytes, 0x4208) != GuestDiagnosticLoadBias + 0x400100)
            return false;
    }
    return true;
}
} // namespace Core::Uwp
