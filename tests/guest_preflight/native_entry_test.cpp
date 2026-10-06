// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <fstream>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include "core/aerolib/aerolib.h"
#include "core/uwp/native_entry_fixture.h"
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#endif

void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value)
        throw std::runtime_error("native entry failed at " + std::to_string(where.line()));
}

void Inspect(std::span<const std::uint8_t> file) {
    const auto payload = Core::Uwp::StageGuestPayload(file);
    const auto manifest = Core::Uwp::InspectGuestLinkManifest(file, true);
    const auto plan = Core::Uwp::PlanNativeEntry(payload, manifest);
    std::cout << "plan_valid=" << plan.valid << ";error=" << plan.error
              << ";entry_offset=" << plan.entry_offset << ";plt_offset=" << plan.plt_offset
              << ";slot_offset=" << plan.slot_offset << ";first_import=" << plan.import.Label();
    if (const auto* name = Core::AeroLib::FindByNid(plan.import.nid.c_str()))
        std::cout << ";first_import_name=" << name->name;
    std::cout << ";guest_entry_called=0;inspection_only=1\n";
}

int main(int argc, char** argv) {
    try {
        using namespace Core::Uwp;
        if (argc == 2) {
            std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
            Check(input.good());
            const auto size = input.tellg();
            Check(size > 0 && static_cast<std::uint64_t>(size) <= GuestPayloadFileLimit);
            std::vector<std::uint8_t> file(static_cast<std::size_t>(size));
            input.seekg(0);
            input.read(reinterpret_cast<char*>(file.data()), size);
            Check(input.good());
            Inspect(file); // User-owned real eboot is never executed on desktop by this tool.
            return 0;
        }
        for (const bool self : {false, true}) {
            const auto file = MakeNativeEntryFixture(self);
            const auto payload = StageGuestPayload(file);
            const auto manifest = InspectGuestLinkManifest(file, true);
            const auto plan = PlanNativeEntry(payload, manifest);
            Check(plan.valid && plan.entry_offset == 0x100 && plan.plt_offset == 0x180 &&
                  plan.slot_offset == 0x4200 && plan.symbol == 1);
            Check(plan.import.nid == "fixture" && plan.import.type == 2);
            for (std::size_t i = 0; i < NativeCrtPrefix.size(); ++i) {
                auto mutated = payload;
                mutated.image.bytes[0x100 + i] ^= 1;
                Check(!PlanNativeEntry(mutated, manifest).valid);
            }
            for (const std::size_t offset : {0x114U, 0x180U, 0x181U}) {
                auto mutated = payload;
                mutated.image.bytes[offset] ^= 1;
                Check(!PlanNativeEntry(mutated, manifest).valid);
            }
            auto mutated = payload;
            std::fill_n(mutated.image.bytes.begin() + 0x115, 4, std::uint8_t{0xff});
            Check(!PlanNativeEntry(mutated, manifest).valid);
            mutated = payload;
            mutated.image.bytes[0x182] += 1; // Unaligned slot.
            Check(!PlanNativeEntry(mutated, manifest).valid);
            auto changed_manifest = manifest;
            changed_manifest.relocation_records.back().type = 6;
            Check(!PlanNativeEntry(payload, changed_manifest).valid);
            changed_manifest = manifest;
            changed_manifest.relocation_records.back().addend_bits = 1;
            Check(!PlanNativeEntry(payload, changed_manifest).valid);
            changed_manifest = manifest;
            changed_manifest.relocation_records.push_back(manifest.relocation_records.back());
            Check(!PlanNativeEntry(payload, changed_manifest).valid);
            changed_manifest = manifest;
            changed_manifest.symbol_records[1].type = 1;
            Check(!PlanNativeEntry(payload, changed_manifest).valid);
            const auto bridge = MakeNativeEntryBridge(0x1122334455667788ULL, 0x8877665544332211ULL);
            Check(bridge.resume_offset < bridge.stop_offset && bridge.code.size() < 4096);
        }
#if defined(__linux__) && defined(__x86_64__)
        // Execute AUTHORIAL bytes to validate the emitter and all-register
        // restoration locally. Not a desktop substitute for hardware validation.
        const auto fixture = MakeNativeEntryFixture(false);
        auto linked = StageGuestDataLink(fixture, GuestDiagnosticLoadBias);
        Check(linked.valid);
        void* image = mmap(nullptr, linked.payload.image.bytes.size(), PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        void* thunk =
            mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        Check(image != MAP_FAILED && thunk != MAP_FAILED);
        NativeEntryCapture capture;
        auto bridge = MakeNativeEntryBridge(reinterpret_cast<std::uint64_t>(&capture), 0);
        const auto stop = reinterpret_cast<std::uint64_t>(thunk) + bridge.stop_offset;
        bridge = MakeNativeEntryBridge(reinterpret_cast<std::uint64_t>(&capture), stop);
        const auto key = PlanNativeEntry(linked.payload, linked.manifest).import;
        const std::array exports{GuestDataExport{key, stop}};
        linked = StageGuestDataLink(
            fixture, reinterpret_cast<std::uint64_t>(image) - linked.payload.image.plan.base, false,
            exports);
        Check(linked.valid);
        std::memcpy(image, linked.payload.image.bytes.data(), linked.payload.image.bytes.size());
        std::memcpy(thunk, bridge.code.data(), bridge.code.size());
        Check(mprotect(image, 4096, PROT_READ | PROT_EXEC) == 0);
        Check(mprotect(thunk, 4096, PROT_READ | PROT_EXEC) == 0);
        Core::EntryParams params{};
        params.argc = 1;
        params.argv[0] = "/app0/eboot.bin";
        params.entry_addr = reinterpret_cast<std::uint64_t>(image) + 0x100;
        using Invoke = void(__attribute__((ms_abi))*)(Core::EntryParams*, void*);
        reinterpret_cast<Invoke>(thunk)(&params, reinterpret_cast<void*>(params.entry_addr));
        Check(capture.reached == 1 && capture.return_ip == params.entry_addr + 25 &&
              capture.argc == 1 &&
              capture.argv == reinterpret_cast<std::uint64_t>(&params.argv[0]) &&
              capture.exit_function == stop &&
              capture.arguments[0] == reinterpret_cast<std::uint64_t>(&params) &&
              capture.guest_rsp % 16 == 8 && capture.saved_host_rsp - capture.guest_rsp == 72);
        Check(munmap(image, linked.payload.image.bytes.size()) == 0 && munmap(thunk, 4096) == 0);
#endif
        std::cout << "native CRT prefix, PLT certification and stop bridge passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
