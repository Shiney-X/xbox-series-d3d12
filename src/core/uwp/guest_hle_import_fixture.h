// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_data_link.h"
#include "core/uwp/guest_hle_fixture.h"
#include "core/uwp/guest_link_fixture.h"

namespace Core::Uwp {
// Closed authored corpus only. Retains the execution/fault leaves while adding
// a real dynamic import and JUMP_SLOT. Never feed commercial code to this runner.
[[nodiscard]] inline std::vector<std::uint8_t> MakeGuestHleImportFixture(bool self = false) {
    auto file = MakeGuestLinkFixture(self);
    const auto code = MakeGuestHleFixture();
    const auto put = [&](std::size_t offset, std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i)
            file[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    const std::size_t header = self ? 128 : 0;
    std::copy_n(code.begin() + 0x100, 0x100, file.begin() + (self ? 0x1000 : 0x400));
    put(header + 96, 0x100); // Code LOAD file/memory sizes, still one RX page.
    put(header + 104, 0x100);
    put((self ? 0x1400 : 0x800) + 3 * 16 + 8, 48); // Null + one function symbol.
    if (self) {
        put(80, 0x100); // SELF block for program index zero.
        put(88, 0x100);
    }
    return file;
}

// The address is selected by the native harness from its owned thunk, not from
// the file. This function produces DATA bindings, not callability certification.
[[nodiscard]] inline GuestDataExport MakeFixtureHleImportExport(std::uint64_t thunk) {
    return {{"fixture", "libFixture", "libFixture", 0, 0, 0, 2}, thunk};
}

[[nodiscard]] inline bool VerifyFixtureHleImportLink(const GuestDataLink& linked,
                                                     std::uint64_t bias, std::uint64_t thunk) {
    if (!thunk || !linked.valid || !linked.complete || linked.bias != bias ||
        !linked.writes_verified || !linked.untouched_verified || linked.relative_applied != 1 ||
        linked.local_applied != 0 || linked.data_import_applied != 1 || linked.pending_imports ||
        linked.pending_bindings || linked.pending_types || !linked.imports.valid ||
        linked.imports.exports != 1 || linked.imports.matched != 1 || linked.imports.unresolved ||
        linked.imports.bindings.size() != 1 || linked.payload.image.bytes.size() != 32768 ||
        linked.payload.image.plan.base != 0x400000 ||
        linked.payload.image.plan.entry_offset != 0x100 ||
        linked.imports.bindings[0].key != MakeFixtureHleImportExport(thunk).key ||
        linked.imports.bindings[0].numeric_address != thunk)
        return false;
    const auto read = [&](std::size_t offset) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i)
            value |= std::uint64_t(linked.payload.image.bytes[offset + i]) << (8 * i);
        return value;
    };
    std::uint64_t entry = 0;
    return AddGuestSigned(bias, 0x400100, entry) && read(FixtureHlePointerOffset) == thunk &&
           read(0x4208) == entry;
}
} // namespace Core::Uwp
