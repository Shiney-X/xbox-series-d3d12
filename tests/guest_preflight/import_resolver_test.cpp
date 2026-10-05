// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <source_location>
#include <stdexcept>
#include "core/uwp/guest_import_fixture.h"

void Check(bool value, std::source_location location = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("import resolver failed at " + std::to_string(location.line()));
}
void Put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value,
         unsigned count = 8) {
    for (unsigned i = 0; i < count; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint64_t Read(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= std::uint64_t(bytes[offset + i]) << (8 * i);
    return value;
}
int main() {
    try {
        using namespace Core::Uwp;
        Check(VerifyGuestImportFixtures());
        std::uint16_t decoded = 0;
        for (std::uint32_t id = 0; id <= UINT16_MAX; ++id)
            Check(DecodeGuestId(EncodeGuestId(static_cast<std::uint16_t>(id)), decoded) &&
                  decoded == id);
        for (const auto text : {"", "AA", "AB", "QAA", "////", "a_b", "=", "AAAA"})
            Check(!DecodeGuestId(text, decoded));
        Check(EncodeGuestId(0) == "A" && EncodeGuestId(63) == "-" && EncodeGuestId(64) == "BA" &&
              EncodeGuestId(4095) == "--" && EncodeGuestId(65535) == "P--");
        const auto exports = MakeGuestImportFixtureExports();
        for (const bool self : {false, true}) {
            const auto file = MakeGuestLinkFixture(self);
            const auto manifest = InspectGuestLinkManifest(file, true);
            const auto empty = ResolveGuestImports(manifest);
            Check(empty.valid && empty.bindings.size() == 2 && empty.unresolved == 2 &&
                  !empty.matched);
            Check(empty.bindings[0].symbol == 1 && empty.bindings[0].library_id == 0 &&
                  empty.bindings[0].module_id == 1 && empty.bindings[0].key == exports[0].key);
            Check(empty.bindings[1].key == exports[1].key);
            Check(ResolveGuestImports(manifest, exports).matched == 2);
            Check(!ResolveGuestImports(InspectGuestLinkManifest(file)).valid);
            for (const auto kind : {0, 1, 2, 3}) {
                auto bad = manifest;
                if (kind == 0)
                    bad.import_symbol_indices[1] = bad.import_symbol_indices[0];
                if (kind == 1)
                    bad.import_symbol_indices[0] = static_cast<std::uint32_t>(bad.symbols);
                if (kind == 2)
                    bad.symbol_records[1].section = 1;
                if (kind == 3)
                    bad.symbol_records[1].binding = 0;
                const auto rejected = ResolveGuestImports(bad, exports);
                Check(!rejected.valid && rejected.bindings.empty() &&
                      rejected.addresses_by_symbol.empty());
            }
            const auto raw = [&](std::size_t offset) {
                return self && offset >= 0x600 ? 0x1200 + offset - 0x600
                       : self                  ? 128 + offset
                                               : offset;
            };
            const auto reject = [&](const auto& bad, const char* error) {
                const auto parsed = InspectGuestLinkManifest(bad, true);
                Check(parsed.valid);
                const auto plan = ResolveGuestImports(parsed, exports);
                Check(!plan.valid && plan.error == error && plan.matched == 0 &&
                      plan.addresses_by_symbol.empty());
                const auto stage = StageGuestDataLink(bad, GuestDiagnosticLoadBias, false, exports);
                Check(!stage.valid && stage.error == "import_namespace_or_registry_invalid" &&
                      stage.relative_applied == 0 && stage.data_import_applied == 0 &&
                      stage.payload.image.bytes == StageGuestPayload(bad).image.bytes);
            };
            auto lib_id = file;
            Put(lib_id, raw(0x628), 'B', 1);
            reject(lib_id, "unknown_import_library_id");
            auto mod_id = file;
            Put(mod_id, raw(0x62a), 'A', 1);
            reject(mod_id, "unknown_import_module_id");
            auto duplicate_module = file;
            Put(duplicate_module, raw(0x800 + 11 * 16), 0x6100000f);
            Put(duplicate_module, raw(0x808 + 11 * 16), 16 | (1ULL << 48));
            reject(duplicate_module, "duplicate_module_id");
            auto duplicate_library = file;
            Put(duplicate_library, raw(0x800 + 11 * 16), 0x61000015);
            Put(duplicate_library, raw(0x808 + 11 * 16), 16);
            reject(duplicate_library, "duplicate_library_id");
            const auto name_case = [&](std::string_view name, const char* error) {
                auto bad = file;
                std::fill_n(bad.begin() + raw(0x620), 16, 0);
                std::copy(name.begin(), name.end(), bad.begin() + raw(0x620));
                reject(bad, error);
            };
            name_case("fixture", "invalid_nid_namespace_shape");
            name_case("fixture#AA#B", "invalid_encoded_import_id");
            name_case("fixture##B", "invalid_encoded_import_id");
            name_case("fixture#A#B#C", "invalid_nid_namespace_shape");
            name_case("#A#B", "invalid_nid_namespace_shape");
            const std::array duplicate_exports{exports[0], exports[0]};
            Check(ResolveGuestImports(manifest, duplicate_exports).error == "duplicate_export_key");
            auto zero = exports;
            zero[0].numeric_address = 0;
            Check(ResolveGuestImports(manifest, zero).error == "invalid_data_export");
            for (const auto kind : {0, 1, 2, 3, 4}) {
                auto invalid = exports;
                if (kind == 0)
                    invalid[0].key.nid.clear();
                if (kind == 1)
                    invalid[0].key.library = "bad;name";
                if (kind == 2)
                    invalid[0].key.module = "bad|name";
                if (kind == 3)
                    invalid[0].key.type = 6;
                if (kind == 4)
                    invalid[0].key.nid.assign(257, 'a');
                Check(ResolveGuestImports(manifest, invalid).error == "invalid_data_export");
            }
            const std::vector<GuestDataExport> too_many(4097);
            Check(ResolveGuestImports(manifest, too_many).error == "export_registry_budget");
            for (const auto kind : {0, 1, 2, 3, 4, 5, 6}) {
                auto wrong = exports;
                if (kind == 0)
                    wrong[0].key.nid = "other";
                if (kind == 1)
                    wrong[0].key.library = "other";
                if (kind == 2)
                    wrong[0].key.module = "other";
                if (kind == 3)
                    wrong[0].key.library_version = 1;
                if (kind == 4)
                    wrong[0].key.module_major = 1;
                if (kind == 5)
                    wrong[0].key.type = 1;
                if (kind == 6)
                    wrong[0].key.module_minor = 1;
                const auto plan = ResolveGuestImports(manifest, wrong);
                Check(plan.valid && plan.matched == 1 && plan.unresolved == 1);
                const auto strict = StageGuestDataLink(file, GuestDiagnosticLoadBias, true, wrong);
                Check(!strict.valid && strict.data_import_applied == 0 &&
                      strict.relative_applied == 0 &&
                      strict.payload.image.bytes == StageGuestPayload(file).image.bytes);
            }
            auto versioned = file;
            Put(versioned, raw(0x808 + 12 * 16),
                16 | (0x12ULL << 32) | (0x34ULL << 40) | (1ULL << 48));
            Put(versioned, raw(0x808 + 13 * 16), 16 | (0x234ULL << 32));
            const auto version_plan =
                ResolveGuestImports(InspectGuestLinkManifest(versioned, true));
            Check(version_plan.valid && version_plan.bindings[0].key.library_version == 0x234 &&
                  version_plan.bindings[0].key.module_major == 0x34 &&
                  version_plan.bindings[0].key.module_minor == 0x12);
            auto compatible = exports;
            for (auto& entry : compatible) {
                entry.key.library_version = 0x234;
                entry.key.module_major = 0x34;
                entry.key.module_minor = 0x12;
            }
            Check(
                StageGuestDataLink(versioned, GuestDiagnosticLoadBias, true, compatible).complete);
            for (const auto type : {1U, 6U, 7U}) {
                auto object = file;
                Put(object, raw(0x720), (2ULL << 32) | type);
                Put(object, raw(0x728), 3);
                const auto resolved =
                    StageGuestDataLink(object, GuestDiagnosticLoadBias, true, exports);
                Check(resolved.valid && resolved.complete && resolved.data_import_applied == 1 &&
                      resolved.writes_verified && resolved.untouched_verified);
                Check(Read(resolved.payload.image.bytes, 0x4200) ==
                      0x200002000ULL + (type == 1 ? 3 : 0));
            }
            auto overflow = file;
            Put(overflow, raw(0x720), (1ULL << 32) | 1);
            Put(overflow, raw(0x728), 1);
            auto high = exports;
            high[0].numeric_address = UINT64_MAX;
            const auto failed = StageGuestDataLink(overflow, GuestDiagnosticLoadBias, false, high);
            Check(!failed.valid && failed.error == "data_import_value_overflow" &&
                  failed.data_import_applied == 0 && failed.relative_applied == 0 &&
                  failed.payload.image.bytes == StageGuestPayload(overflow).image.bytes);
        }
        std::cout << "Typed/versioned import binding and numeric-data relocation contracts passed; "
                     "no HLE calls\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
