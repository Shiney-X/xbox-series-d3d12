// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <fstream>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include "core/uwp/guest_data_link.h"
#include "core/uwp/guest_payload_fixture.h"

void Check(bool value, std::source_location location = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("guest loader contract failed at line " +
                                 std::to_string(location.line()));
}
void Put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value,
         unsigned count = 8) {
    for (unsigned i = 0; i < count; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
int main(int argc, char** argv) {
    try {
        using namespace Core::Uwp;
        // Optional read-only diagnostic on a locally owned executable; no bytes
        // are emitted, saved, redistributed or executed by this utility.
        if (argc == 2) {
            std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
            Check(input.good());
            const auto size = input.tellg();
            Check(size >= 0 && static_cast<std::uint64_t>(size) <= GuestPayloadFileLimit);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            input.seekg(0);
            input.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            Check(input.good());
            const auto result = StageGuestDataLink(bytes);
            std::cout << result.payload.Details() << ";payload_phase=pre_relocation\n";
            std::cout << result.manifest.Details() << '\n';
            std::cout << result.imports.Details() << '\n';
            std::cout << result.Details() << '\n';
            return result.valid ? 0 : 1;
        }
        Check(argc == 1);
        const auto fixture = MakeGuestLoaderFixture();
        Check(VerifyGuestLoaderFixture(StageRawGuest(fixture)));
        Check(VerifyGuestLoaderFixture(StageGuestPayload(fixture).image));
        const auto payload_file = MakeGuestPayloadFixture();
        const auto payload = StageGuestPayload(payload_file);
        Check(payload.Ready() && !payload.image.plan.raw_elf && payload.copied_bytes == 8 &&
              payload.bss_bytes == 12 && VerifyGuestLoaderFixture(payload.image));
        for (std::size_t length = 0; length < payload_file.size(); ++length) {
            const auto bad = StageGuestPayload(std::span(payload_file).first(length));
            Check(!bad.Ready() && bad.image.bytes.empty());
        }
        const auto reject_payload = [&](std::size_t offset, std::uint64_t value, unsigned count,
                                        const char* expected) {
            auto bad = payload_file;
            Put(bad, offset, value, count);
            const auto result = StageGuestPayload(bad);
            Check(!result.Ready() && result.image.bytes.empty() && result.image.error == expected);
        };
        reject_payload(32, (1ULL << 20) | 0x802, 8, "encrypted_or_compressed_self_unsupported");
        reject_payload(32, (1ULL << 20) | 0x808, 8, "encrypted_or_compressed_self_unsupported");
        reject_payload(32, (2ULL << 20) | 0x800, 8, "self_program_index_outside_table");
        reject_payload(32, 0x800, 8, "duplicate_self_program_block");
        reject_payload(32, (1ULL << 20), 8, "missing_self_load_block");
        reject_payload(40, 0x100, 8, "self_payload_outside_file");
        reject_payload(40, UINT64_MAX, 8, "self_payload_outside_file");
        reject_payload(48, UINT64_MAX, 8, "self_payload_outside_file");
        reject_payload(40, 0x180, 8, "overlapping_self_payloads");
        reject_payload(56, 5, 8, "unsupported_self_block_size");
        reject_payload(12, 0x100, 2, "invalid_self_metadata_range");
        reject_payload(12, 0xffff, 2, "invalid_self_metadata_range");
        auto mismatch = payload_file;
        Put(mismatch, 48, 3);
        Put(mismatch, 56, 3);
        Check(StageGuestPayload(mismatch).image.error == "self_logical_range_or_size_invalid");
        auto bss_only = payload_file;
        Put(bss_only, 96 + 120 + 32, 0);
        Put(bss_only, 32, (1ULL << 20)); // No blocked file payload for BSS-only LOAD.
        const auto bss_payload = StageGuestPayload(bss_only);
        Check(bss_payload.Ready() && bss_payload.copied_bytes == 4 && bss_payload.bss_bytes == 16);
        Check(std::all_of(bss_payload.image.bytes.begin() + 0x4200,
                          bss_payload.image.bytes.begin() + 0x4210,
                          [](std::uint8_t value) { return value == 0; }));
        auto relro = payload_file;
        Put(relro, 96 + 120, 0x61000010, 4);
        const auto relro_payload = StageGuestPayload(relro);
        Check(relro_payload.Ready() && relro_payload.image.plan.segments.size() == 2 &&
              relro_payload.image.plan.segments[1].type == 0x61000010 &&
              VerifyGuestLoaderFixture(relro_payload.image));
        Check(PlanGuestLoads(relro, relro.size()).segments.size() == 1);
        Put(relro, 96 + 124, 5, 4);
        Check(StageGuestPayload(relro).image.error == "unsupported_relro_permissions");
        // A non-LOAD header between LOADs must not turn ordinal 1 into ID 1.
        auto sparse = payload_file;
        std::copy_n(payload_file.begin() + 96 + 120, 56, sparse.begin() + 96 + 176);
        Put(sparse, 96 + 120, 0, 4);
        Put(sparse, 96 + 56, 3, 2);
        Put(sparse, 12, 0x160, 2);
        Put(sparse, 32, (2ULL << 20) | 0x800);
        const auto sparse_payload = StageGuestPayload(sparse);
        Check(sparse_payload.Ready() && sparse_payload.image.plan.segments[1].program_index == 2 &&
              VerifyGuestLoaderFixture(sparse_payload.image));
        auto changed_payload = payload_file;
        changed_payload[0x180] ^= 1;
        const auto changed = StageGuestPayload(changed_payload);
        Check(changed.Ready() && changed.checksum != payload.checksum &&
              !VerifyGuestLoaderFixture(changed.image));
        std::vector<std::uint8_t> oversized(static_cast<std::size_t>(GuestPayloadFileLimit + 1));
        Check(StageGuestPayload(oversized).image.error == "payload_file_budget_exceeded");
        for (std::size_t length = 0; length < fixture.size(); ++length) {
            const auto truncated = StageRawGuest(std::span(fixture).first(length));
            Check(!truncated.error.empty() && truncated.bytes.empty());
        }
        const auto reject = [&](std::size_t offset, std::uint64_t value, unsigned count,
                                const char* expected) {
            auto bad = fixture;
            Put(bad, offset, value, count);
            const auto image = StageRawGuest(bad);
            Check(image.bytes.empty() && image.error == expected);
        };
        reject(68, 7, 4, "unsupported_segment_permissions");
        reject(68, 8, 4, "unsupported_segment_permissions");
        reject(96, 5, 8, "file_size_exceeds_memory");
        reject(112, 3, 8, "invalid_segment_alignment");
        auto overflowing = fixture;
        Put(overflowing, 112, 1);
        Put(overflowing, 80, ~std::uint64_t{0} - 1);
        Check(StageRawGuest(overflowing).error == "segment_address_overflow_or_budget");
        reject(72, 0x1000, 8, "segment_outside_file");
        reject(136, 0x400100, 8, "overlapping_load_segments");
        reject(136, 0x400100 + GuestImageLimit, 8, "image_budget_exceeded");
        reject(24, 0x404200, 8, "entry_outside_file_backed_executable_segment");
        reject(24, 0x400104, 8, "entry_outside_file_backed_executable_segment");
        auto no_load = fixture;
        Put(no_load, 64, 0, 4);
        Put(no_load, 120, 0, 4);
        Check(StageRawGuest(no_load).error == "no_load_segments");
        auto many = fixture;
        Put(many, 56, 129, 2);
        Check(PlanGuestLoads(many, 10000).error == "program_header_limit");
        Check(PlanGuestLoads(std::span(fixture).first(120), fixture.size()).error ==
              "program_headers_outside_prefix");
        // SELF metadata may form a plan, but must never use logical offsets as payload offsets.
        std::vector<std::uint8_t> self(32, 0);
        Put(self, 0, 0x1d3d154f, 4);
        self[5] = self[6] = self[8] = self[9] = 1;
        self[7] = 0x12;
        self.insert(self.end(), fixture.begin(), fixture.end());
        Check(PlanGuestLoads(self, self.size()).valid);
        Check(StageRawGuest(self).error == "self_payload_adapter_required");
        Check(StageRawGuest(self).bytes.empty());
        // Canary: corruption is detectable by the independent byte oracle.
        auto staged = StageRawGuest(fixture);
        staged.bytes[0x4204] = 1;
        Check(!VerifyGuestLoaderFixture(staged));
        std::cout
            << "PT_LOAD data staging, BSS, budgets and rejection gates passed; no execution\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
