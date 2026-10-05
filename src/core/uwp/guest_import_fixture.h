// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include "core/uwp/guest_data_link.h"
#include "core/uwp/guest_link_fixture.h"

namespace Core::Uwp {
// Authorial fixture keys and numeric values only. Never register these for
// games or cast them into HLE callbacks. They certify lookup/writes, not calls.
[[nodiscard]] inline std::array<GuestDataExport, 2> MakeGuestImportFixtureExports() {
    return {{{{"fixture", "libFixture", "libFixture", 0, 0, 0, 2}, 0x200001000ULL},
             {{"object", "libFixture", "libFixture", 0, 0, 0, 1}, 0x200002000ULL}}};
}

[[nodiscard]] inline bool VerifyGuestImportFixtures() {
    const auto exports = MakeGuestImportFixtureExports();
    for (const bool self : {false, true}) {
        const auto file = MakeGuestLinkFixture(self);
        const auto result = StageGuestDataLink(file, GuestDiagnosticLoadBias, true, exports);
        if (!result.valid || !result.complete || !result.imports.valid ||
            result.imports.bindings.size() != 2 || result.imports.matched != 2 ||
            result.imports.unresolved != 0 || result.relative_applied != 1 ||
            result.data_import_applied != 1 || !result.writes_verified ||
            !result.untouched_verified)
            return false;
        std::uint64_t slot = 0;
        for (unsigned i = 0; i < 8; ++i)
            slot |= std::uint64_t(result.payload.image.bytes[0x4200 + i]) << (i * 8);
        if (slot != 0x200001000ULL)
            return false;
        auto wrong = exports;
        wrong[0].key.library_version = 1;
        const auto rejected = StageGuestDataLink(file, GuestDiagnosticLoadBias, true, wrong);
        if (rejected.valid || rejected.error != "pending_relocations_require_resolver" ||
            rejected.relative_applied || rejected.data_import_applied)
            return false;
    }
    return true;
}
} // namespace Core::Uwp
