// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <source_location>
#include <stdexcept>
#include "core/uwp/guest_data_link.h"
#include "core/uwp/guest_data_link_fixture.h"
#include "core/uwp/guest_link_fixture.h"

void Check(bool value, std::source_location location = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("data relocation failed at " + std::to_string(location.line()));
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
        Check(VerifyGuestDataLinkFixtures());
        std::uint64_t sum = 0;
        Check(AddGuestSigned(20, 5, sum) && sum == 25);
        Check(AddGuestSigned(20, UINT64_MAX - 4, sum) && sum == 15);
        Check(!AddGuestSigned(UINT64_MAX, 1, sum));
        Check(!AddGuestSigned(4, UINT64_MAX - 4, sum));
        Check(AddGuestSigned(1ULL << 63, 1ULL << 63, sum) && sum == 0);
        Check(!AddGuestSigned((1ULL << 63) - 1, 1ULL << 63, sum));
        for (const bool self : {false, true}) {
            const auto file = MakeGuestLinkFixture(self);
            const auto original = StageGuestPayload(file);
            const auto image_copy = original.image.bytes;
            const auto partial = StageGuestDataLink(file);
            Check(partial.valid && !partial.complete && partial.relative_applied == 1 &&
                  partial.local_applied == 0 && partial.pending_imports == 1 &&
                  partial.writes_verified && partial.untouched_verified);
            Check(Read(partial.payload.image.bytes, 0x4208) == GuestDiagnosticLoadBias + 0x400100);
            Check(Read(partial.payload.image.bytes, 0x4200) == Read(image_copy, 0x4200));
            Check(original.image.bytes == image_copy);
            Check(partial.payload.checksum == original.checksum &&
                  partial.checksum_after != original.checksum);
            const auto strict = StageGuestDataLink(file, GuestDiagnosticLoadBias, true);
            Check(!strict.valid && strict.error == "pending_relocations_require_resolver" &&
                  strict.relative_applied == 0 && strict.payload.image.bytes == image_copy);
            const auto raw = [&](std::size_t offset) {
                return self && offset >= 0x600 ? 0x1200 + offset - 0x600
                       : self                  ? 128 + offset
                                               : offset;
            };
            const auto reject = [&](std::size_t offset, std::uint64_t value, unsigned count,
                                    const char* error) {
                auto bad = file;
                Put(bad, raw(offset), value, count);
                const auto before = StageGuestPayload(bad);
                const auto result = StageGuestDataLink(bad);
                Check(!result.valid && result.error == error && result.relative_applied == 0 &&
                      result.local_applied == 0 &&
                      result.payload.image.bytes == before.image.bytes);
            };
            auto overflow = file;
            Put(overflow, raw(0x710), UINT64_MAX / 2);
            const auto overflow_result = StageGuestDataLink(overflow, 0x8000000001000000ULL);
            Check(!overflow_result.valid && overflow_result.error == "relative_value_overflow" &&
                  overflow_result.payload.image.bytes == StageGuestPayload(overflow).image.bytes);
            reject(0x710, 1ULL << 63, 8, "relative_value_overflow");
            reject(0x718, 0x404208, 8, "overlapping_relocation_targets");
            reject(0x718, 0x404204, 8, "overlapping_relocation_targets");
            reject(64 + 56 + 4, 4, 4, "selected_write_not_allowed");
            reject(0x720, (1ULL << 32) | 42, 8, "unsupported_relocation_type");
            reject(0x700, 0x402000, 8, "payload_or_manifest_invalid");
            auto text_write = file;
            Put(text_write, raw(64 + 40), 8); // Enough memory, but target is RX code.
            Put(text_write, raw(0x700), 0x400100);
            const auto text_result = StageGuestDataLink(text_write);
            Check(!text_result.valid && text_result.error == "selected_write_not_allowed" &&
                  text_result.payload.image.bytes == StageGuestPayload(text_write).image.bytes);
            auto negative = file;
            Put(negative, raw(0x710), UINT64_MAX - 4);
            const auto neg_result = StageGuestDataLink(negative);
            Check(neg_result.valid &&
                  Read(neg_result.payload.image.bytes, 0x4208) == GuestDiagnosticLoadBias - 5);
            auto relro = file;
            Put(relro, raw(64 + 56), 0x61000010, 4);
            Put(relro, raw(64 + 56 + 4), 4, 4);
            Check(StageGuestDataLink(relro).valid); // Read-only RELRO is writable in staging.
            auto local = file;
            Put(local, raw(0x698 + 4), 2, 1); // STB_LOCAL, STT_FUNC.
            Put(local, raw(0x698 + 6), 1, 2);
            Put(local, raw(0x698 + 8), 0x400100);
            for (const auto type : {1U, 6U, 7U}) {
                Put(local, raw(0x720), (1ULL << 32) | type);
                Put(local, raw(0x728), 3);
                const auto result = StageGuestDataLink(local, GuestDiagnosticLoadBias, true);
                Check(result.valid && result.complete && result.local_applied == 1 &&
                      result.relative_applied == 1 && result.pending_imports == 0);
                Check(Read(result.payload.image.bytes, 0x4200) ==
                      GuestDiagnosticLoadBias + 0x400100 + (type == 1 ? 3 : 0));
            }
            auto bad_local = local;
            Put(bad_local, raw(0x698 + 8), 0x402000);
            const auto failed_local = StageGuestDataLink(bad_local);
            Check(!failed_local.valid &&
                  failed_local.error == "local_symbol_outside_image_or_overflow" &&
                  failed_local.payload.image.bytes == StageGuestPayload(bad_local).image.bytes);
            auto local_overflow = local;
            Put(local_overflow, raw(0x720), (1ULL << 32) | 1);
            Put(local_overflow, raw(0x728), UINT64_MAX / 2);
            const auto overflow_local = StageGuestDataLink(local_overflow, 0x8000000001000000ULL);
            Check(!overflow_local.valid && overflow_local.error == "local_symbol_value_overflow" &&
                  overflow_local.payload.image.bytes ==
                      StageGuestPayload(local_overflow).image.bytes);
            Put(local, raw(0x698 + 4), 0x12, 1); // Defined global still needs resolver.
            Check(StageGuestDataLink(local).pending_bindings == 1);
            auto tls = file;
            Put(tls, raw(0x720), (1ULL << 32) | 16);
            Check(StageGuestDataLink(tls).valid && StageGuestDataLink(tls).pending_types == 1);
            auto none = file;
            Put(none, raw(0x720), 0);
            Check(StageGuestDataLink(none).complete && StageGuestDataLink(none).none == 1);
            Check(StageGuestDataLink(none).Details().find("ready_for_boot=0") != std::string::npos);
            Check(!StageGuestDataLink(file, UINT64_MAX).valid);
            Check(StageGuestDataLink(file, 0).valid);
            for (std::size_t length = 0; length < file.size(); ++length)
                Check(!StageGuestDataLink(std::span(file).first(length)).valid);
        }
        std::cout << "Transactional relative/local64 data relocation contracts passed; no boot\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
