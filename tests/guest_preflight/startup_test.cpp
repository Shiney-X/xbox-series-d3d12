// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <source_location>
#include <stdexcept>
#include "core/uwp/guest_link_fixture.h"
#include "core/uwp/guest_startup.h"

void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("startup failed at " + std::to_string(where.line()));
}
int main() {
    try {
        using namespace Core::Uwp;
        alignas(32) std::array<std::uint8_t, GuestStartupStackSize> stack{};
        alignas(32) std::array<std::uint8_t, 128> tls{};
        const std::array<std::uint8_t, 8> initial{1, 2, 3, 4, 5, 6, 7, 8};
        for (const unsigned memory : {0U, 16U, 32U}) {
            const auto source =
                memory ? std::span<const std::uint8_t>(initial) : std::span<const std::uint8_t>{};
            const auto prepared = PrepareGuestStartup(stack, tls, source, memory, 8, 0x200001000ULL,
                                                      "/app0/eboot.bin");
            Check(prepared.valid && prepared.initial_rsp % 16 == 8);
            Core::EntryParams params{};
            Core::Tcb tcb{};
            std::array<Core::DtvEntry, 3> dtv{};
            std::memcpy(&params, stack.data() + 0x100, sizeof(params));
            std::memcpy(&tcb, reinterpret_cast<const void*>(prepared.tcb), sizeof(tcb));
            std::memcpy(dtv.data(), reinterpret_cast<const void*>(prepared.dtv), sizeof(dtv));
            Check(params.argc == 1 && params.padding == 0 && params.entry_addr == 0x200001000ULL);
            Check(params.argv[1] == nullptr && std::strcmp(params.argv[0], "/app0/eboot.bin") == 0);
            Check(std::memcmp(reinterpret_cast<const void*>(prepared.initial_rsp), &params, 16) ==
                  0);
            Check(reinterpret_cast<std::uint64_t>(tcb.tcb_self) == prepared.tcb &&
                  reinterpret_cast<std::uint64_t>(tcb.tcb_dtv) == prepared.dtv && !tcb.tcb_thread &&
                  !tcb.tcb_canary && !tcb.tcb_fiber);
            Check(dtv[0].counter == 1 && dtv[1].counter == 1 &&
                  reinterpret_cast<std::uint64_t>(dtv[2].pointer) == (memory ? prepared.tls : 0));
            Check(source.empty() || std::equal(source.begin(), source.end(), tls.begin()));
            Check(std::all_of(tls.begin() + source.size(), tls.begin() + memory,
                              [](auto byte) { return byte == 0; }));
        }
        const auto before_stack = stack;
        const auto before_tls = tls;
        for (unsigned variant = 0; variant < 8; ++variant) {
            auto stack_span = std::span(stack);
            auto tls_span = std::span(tls);
            std::uint64_t memory = 16, alignment = 8, entry = 123;
            std::string_view argument = "/app0/eboot.bin";
            switch (variant) {
            case 0:
                memory = 7;
                break;
            case 1:
                alignment = 64;
                break;
            case 2:
                alignment = 3;
                break;
            case 3:
                entry = 0;
                break;
            case 4:
                argument = "";
                break;
            case 5:
                memory = 1024;
                break;
            case 6:
                argument = std::string_view("a\0b", 3);
                break;
            case 7:
                alignment = 0;
                break;
            }
            Check(!PrepareGuestStartup(stack_span, tls_span, initial, memory, alignment, entry,
                                       argument)
                       .valid);
            Check(stack == before_stack && tls == before_tls);
        }
        Check(
            !PrepareGuestStartup(stack, tls, std::span(tls).first(8), 16, 8, 123, "/app0/eboot.bin")
                 .valid);
        Check(stack == before_stack && tls == before_tls);
        for (const bool self : {false, true}) {
            const auto payload = StageGuestPayload(MakeGuestLinkFixture(self));
            const auto pages = StartupPageFlags(payload.image.plan);
            Check(pages.size() == 8 && pages[0] == 5 && pages[1] == 0 && pages[4] == 6);
            auto mixed = payload.image.plan;
            mixed.segments.push_back({0, mixed.base + 0x800, 0, 16, 6, 0, 1});
            Check(StartupPageFlags(mixed).empty());
            mixed = payload.image.plan;
            mixed.segments[0].memory_size = UINT64_MAX;
            Check(StartupPageFlags(mixed).empty());
        }
        std::cout << "shared Orbis startup layouts, TLS data and page plan passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
