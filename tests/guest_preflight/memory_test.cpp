// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <source_location>
#include <stdexcept>
#include "core/uwp/guest_memory_fixture.h"

void Check(bool value, std::source_location location = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("guest memory failed at " + std::to_string(location.line()));
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
        struct {
            std::array<std::uint8_t, 64> data{}, gap{}, source{};
        } backing;
        auto& data = backing.data;
        auto& source = backing.source;
        const auto address = [](auto* pointer) {
            return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer));
        };
        const std::array ranges{GuestMemoryRange{data, true}, GuestMemoryRange{source, false}};
        const GuestMemory memory(ranges);
        const auto dest = address(data.data()), src = address(source.data());
        Check(memory.Valid() && !GuestMemory({}).Valid());
        const std::array duplicate{ranges[0], ranges[0]};
        Check(!GuestMemory(duplicate).Valid());
        const std::array overlap{ranges[0], GuestMemoryRange{std::span(data).subspan(1), false}};
        Check(!GuestMemory(overlap).Valid());
        // Complete matrix of starts/lengths: exact-end accepted, boundary+1 rejected,
        // with a full-buffer oracle on every accepted/rejected write.
        for (unsigned offset = 0; offset <= data.size(); ++offset) {
            for (unsigned count = 0; count <= data.size() + 1; ++count) {
                data.fill(0x55);
                auto expected = data;
                const bool valid = count <= data.size() - offset;
                if (valid)
                    std::fill_n(expected.begin() + offset, count, std::uint8_t{0xa5});
                const auto result =
                    memory.Invoke(GuestMemoryService::Set, dest + offset, 0x12345678a5ULL, count);
                Check(result.valid == valid && data == expected);
                if (valid)
                    Check(result.value == dest + offset);
            }
        }
        for (unsigned i = 0; i < source.size(); ++i)
            source[i] = static_cast<std::uint8_t>(i);
        Check(memory.Invoke(GuestMemoryService::Copy, dest, src, 64).value == dest &&
              data == source);
        Check(memory.Invoke(GuestMemoryService::Compare, dest, src, 64).value == 0);
        data[17]++;
        Check(memory.Invoke(GuestMemoryService::Compare, dest, src, 64).value == 1);
        Check(memory.Invoke(GuestMemoryService::Compare, src, dest, 64).value == UINT64_MAX);
        const auto before = data;
        for (const auto service :
             {GuestMemoryService::Copy, GuestMemoryService::Set, GuestMemoryService::Compare}) {
            Check(!memory.Invoke(service, 0, src, 0).valid);
            Check(!memory.Invoke(service, UINT64_MAX, src, 16).valid);
            Check(!memory.Invoke(service, dest, src, UINT64_MAX).valid);
            Check(!memory.Invoke(service, dest, src, GuestMemory::MaxTransfer + 1).valid);
            Check(memory.Invoke(service, dest + 64, src + 64, 0).valid);
        }
        Check(!memory.Invoke(GuestMemoryService::Set, src, 0, 16).valid);
        Check(!memory.Invoke(GuestMemoryService::Set, src, 0, 0).valid);
        Check(!memory.Invoke(GuestMemoryService::Copy, dest, dest, 16).valid);
        Check(!memory.Invoke(GuestMemoryService::Copy, dest + 1, dest, 16).valid);
        Check(!memory.Invoke(GuestMemoryService::Copy, dest, dest + 1, 16).valid);
        Check(!memory.Invoke(GuestMemoryService::Copy, dest, 0, 16).valid);
        Check(!memory.Invoke(static_cast<GuestMemoryService>(99), dest, src, 0).valid);
        Check(data == before);
        // Adjacent regions must not be joined, even with identical permissions.
        const std::array split{GuestMemoryRange{std::span(data).first(32), true},
                               GuestMemoryRange{std::span(data).subspan(32), true}};
        Check(GuestMemory(split).Valid());
        Check(!GuestMemory(split).Invoke(GuestMemoryService::Set, dest + 31, 0, 2).valid);
        Check(data == before);
        const std::array mixed{split[0], GuestMemoryRange{std::span(data).subspan(32), false}};
        Check(GuestMemory(mixed).Valid());
        Check(!GuestMemory(mixed).Invoke(GuestMemoryService::Set, dest + 32, 0, 0).valid);
        Check(
            GuestMemory(mixed).Invoke(GuestMemoryService::Compare, dest + 32, dest + 32, 0).valid);
        for (const bool self : {false, true}) {
            const auto file = MakeGuestMemoryFixture(self);
            std::array<GuestDataExport, 3> registry;
            for (unsigned i = 0; i < 3; ++i)
                registry[i] = {GuestMemoryKey(i), 0x200000000ULL + i * 64};
            const auto linked = StageGuestDataLink(file, GuestDiagnosticLoadBias, true, registry);
            Check(linked.valid && linked.complete && linked.imports.matched == 3 &&
                  linked.data_import_applied == 3 && linked.relative_applied == 1 &&
                  linked.writes_verified && linked.untouched_verified);
            for (unsigned i = 0; i < 3; ++i) {
                Check(linked.imports.bindings[i].key == registry[i].key);
                Check(Read(linked.payload.image.bytes, 0x4200 + i * 8) ==
                      registry[i].numeric_address);
                const auto entry = 0x140 + i * 16;
                Check(linked.payload.image.bytes[entry] == 0xba &&
                      linked.payload.image.bytes[entry + 1] == 16 &&
                      linked.payload.image.bytes[entry + 5] == 0xff &&
                      linked.payload.image.bytes[entry + 6] == 0x25 &&
                      linked.payload.image.bytes[entry + 65] == 0);
            }
            registry[0].key.library_version = 2;
            const auto rejected = StageGuestDataLink(file, GuestDiagnosticLoadBias, true, registry);
            Check(!rejected.valid && !rejected.data_import_applied &&
                  rejected.payload.image.bytes == StageGuestPayload(file).image.bytes);
        }
        const auto thunk = MakeGuestMemoryThunk(123, 456);
        Check(thunk.code.size() == 41 && thunk.epilogue_offset == 36 &&
              thunk.unwind == std::vector<std::uint8_t>({1, 4, 1, 0, 4, 0x42, 0, 0}));
        std::cout << "bounded guest memory and raw/SELF imports passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
