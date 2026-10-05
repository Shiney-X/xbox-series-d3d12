// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <source_location>
#include <stdexcept>
#include "core/uwp/guest_link_fixture.h"

void Check(bool value, std::source_location location = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("link manifest failed at " + std::to_string(location.line()));
}
void Put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value,
         unsigned count = 8) {
    for (unsigned i = 0; i < count; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
int main() {
    try {
        using namespace Core::Uwp;
        for (const bool self : {false, true}) {
            const auto file = MakeGuestLinkFixture(self);
            const auto manifest = InspectGuestLinkManifest(file);
            Check(VerifyGuestLinkFixture(manifest));
            Check(manifest.valid && manifest.dynamic_entries == 14 && manifest.symbols == 3 &&
                  manifest.undefined_functions == 1 && manifest.undefined_objects == 1 &&
                  manifest.relocations == 2 && manifest.relative == 1 && manifest.jump_slots == 1 &&
                  manifest.target_checks == 2 && manifest.tls_memory_bytes == 8 &&
                  manifest.tls_file_bytes == 0);
            Check(manifest.needed == std::vector<std::string>{"libFixture.prx"} &&
                  manifest.modules == std::vector<std::string>{"libFixture"} &&
                  manifest.libraries == std::vector<std::string>{"libFixture"} &&
                  manifest.imports == std::vector<std::string>{"fixture#A#B", "object#A#B"});
            const auto reject = [&](std::size_t raw_offset, std::uint64_t value, unsigned count,
                                    const char* expected) {
                auto bad = file;
                const auto offset = self ? 0x1200 + raw_offset - 0x600 : raw_offset;
                Put(bad, offset, value, count);
                const auto result = InspectGuestLinkManifest(bad);
                Check(!result.valid && result.error == expected);
            };
            reject(0x808, UINT64_MAX, 8, "invalid_or_missing_string_symbol_tables");
            reject(0x848, 16, 8, "invalid_or_missing_string_symbol_tables"); // SYMENT.
            reject(0x808 + 3 * 16, 71, 8, "invalid_or_missing_string_symbol_tables");
            reject(0x800 + 4 * 16, 0x61000035, 8, "duplicate_link_table_tag");
            reject(0x800 + 14 * 16, 1, 8, "unterminated_dynamic_table");
            reject(0x808 + 11 * 16, 64, 8, "invalid_dependency_name");
            reject(0x698, 64, 4, "invalid_import_name");
            reject(0x808 + 7 * 16, 16, 8, "invalid_relocation_table_layout");
            reject(0x808 + 10 * 16, 17, 8, "invalid_relocation_table_layout");
            auto empty_rela = file;
            const auto dynamic_start = self ? 0x1400U : 0x800U;
            Put(empty_rela, dynamic_start + 5 * 16 + 8, 0x401);
            Put(empty_rela, dynamic_start + 6 * 16 + 8, 0);
            Check(InspectGuestLinkManifest(empty_rela).error == "invalid_relocation_table_layout");
            Put(empty_rela, dynamic_start + 5 * 16 + 8, 0x400);
            const auto empty_result = InspectGuestLinkManifest(empty_rela);
            Check(empty_result.valid && empty_result.relocations == 1);
            auto unknown = file;
            const auto relocation_start = self ? 0x1300U : 0x700U;
            Put(unknown, relocation_start, 0x402000);
            Put(unknown, relocation_start + 8, 42);
            const auto unknown_result = InspectGuestLinkManifest(unknown);
            Check(unknown_result.valid && unknown_result.other_relocations == 1 &&
                  unknown_result.target_checks == 1 &&
                  unknown_result.Details().find("ready_for_boot=0") != std::string::npos);
            reject(0x720, (3ULL << 32) | 7, 8, "invalid_relocation_symbol");
            reject(0x708, (1ULL << 32) | 8, 8, "invalid_relocation_symbol");
            reject(0x700, 0x40420f, 8, "relocation_target_outside_mapped_segment");
            reject(0x700, 0x402000, 8, "relocation_target_outside_mapped_segment");
            auto bad_name = file;
            const auto string_start = self ? 0x1200 : 0x600;
            std::fill_n(bad_name.begin() + string_start, 64, 'x');
            Check(InspectGuestLinkManifest(bad_name).error == "invalid_dependency_name");
            auto tls = file;
            const auto elf_start = self ? 128U : 0U;
            if (self) {
                auto overlap = file;
                Put(overlap, elf_start + 64 + 2 * 56 + 8, 0x400);
                Check(InspectGuestLinkManifest(overlap).error == "payload_layout_invalid");
            }
            Put(tls, elf_start + 64 + 4 * 56 + 48, 3);
            Check(InspectGuestLinkManifest(tls).error == "invalid_tls_layout");
            Put(tls, elf_start + 64 + 4 * 56 + 48, 8);
            Put(tls, elf_start + 64 + 4 * 56 + 16, 0x402000);
            Check(InspectGuestLinkManifest(tls).error == "tls_image_unmapped");
            for (std::size_t length = 0; length < file.size(); ++length)
                Check(!InspectGuestLinkManifest(std::span(file).first(length)).valid);
        }
        Check(!InspectGuestLinkManifest(MakeGuestLoaderFixture()).valid);
        std::cout << "Orbis link manifest contracts passed; no linking/execution\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
