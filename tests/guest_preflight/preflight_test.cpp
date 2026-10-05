// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <stdexcept>
#include <vector>
#include "core/uwp/guest_preflight.h"

void Check(bool condition) {
    if (!condition)
        throw std::runtime_error("guest preflight contract failed");
}
void Put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value,
         unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::vector<std::uint8_t> Elf() {
    std::vector<std::uint8_t> bytes(120);
    Put(bytes, 0, 0x464c457f, 4);
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    bytes[7] = 9;
    bytes[8] = 0;
    Put(bytes, 16, 0xfe10, 2);
    Put(bytes, 18, 62, 2);
    Put(bytes, 20, 1, 4);
    Put(bytes, 24, 0x400000, 8);
    Put(bytes, 32, 64, 8);
    Put(bytes, 52, 64, 2);
    Put(bytes, 54, 56, 2);
    Put(bytes, 56, 1, 2);
    return bytes;
}
int main() {
    try {
        using Core::Uwp::InspectGuestPrefix;
        auto bytes = Elf();
        const auto good = InspectGuestPrefix(bytes, bytes.size());
        Check(good.header_valid && good.orbis_identity && good.entry == 0x400000 &&
              !good.self_container);
        Check(good.Details().find("guest_executed=0") != std::string::npos);
        // Header-only success explicitly does not certify executable segments.
        for (std::size_t n = 0; n < 64; ++n)
            Check(!InspectGuestPrefix(std::span(bytes).first(n), bytes.size()).header_valid);
        for (const std::size_t offset : {0u, 4u, 5u, 6u, 18u, 20u, 52u, 54u, 56u}) {
            auto bad = bytes;
            bad[offset] = 0;
            Check(!InspectGuestPrefix(bad, bad.size()).header_valid);
        }
        auto bad = bytes;
        Put(bad, 32, ~std::uint64_t{0}, 8);
        Check(!InspectGuestPrefix(bad, bad.size()).header_valid);
        bad = bytes;
        Put(bad, 56, 0xffff, 2);
        Check(!InspectGuestPrefix(bad, bad.size()).header_valid);
        Check(!InspectGuestPrefix(bytes, 119).header_valid);
        bytes[7] = 0;
        Check(InspectGuestPrefix(bytes, bytes.size()).header_valid &&
              !InspectGuestPrefix(bytes, bytes.size()).orbis_identity);
        std::vector<std::uint8_t> self(64);
        Put(self, 0, 0x1d3d154f, 4);
        self[5] = 1;
        self[6] = 1;
        self[7] = 0x12;
        self[8] = 1;
        self[9] = 1;
        Put(self, 24, 1, 2);
        Put(self, 32, 10, 8);
        bytes = Elf();
        self.insert(self.end(), bytes.begin(), bytes.end());
        const auto wrapped = InspectGuestPrefix(self, self.size());
        Check(wrapped.header_valid && wrapped.self_container && wrapped.elf_offset == 64 &&
              wrapped.encrypted_segments && wrapped.compressed_segments);
        auto extended = self;
        Put(extended, 64 + 52, 184, 2);
        Check(InspectGuestPrefix(extended, extended.size()).header_valid);
        for (std::size_t n = 0; n < 128; ++n)
            Check(!InspectGuestPrefix(std::span(self).first(n), self.size()).header_valid);
        Put(self, 24, 0xffff, 2);
        Check(!InspectGuestPrefix(self, self.size()).header_valid);
        std::vector<std::uint8_t> large(Core::Uwp::GuestPrefixLimit + 1);
        Check(!InspectGuestPrefix(large, large.size()).header_valid);
        std::cout << "ELF/SELF bounded preflight passed; no guest execution\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
