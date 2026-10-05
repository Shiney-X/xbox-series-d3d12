// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <source_location>
#include <stdexcept>
#include "core/uwp/guest_loader_fixture.h"

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
int main() {
    try {
        using namespace Core::Uwp;
        const auto fixture = MakeGuestLoaderFixture();
        Check(VerifyGuestLoaderFixture(StageRawGuest(fixture)));
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
