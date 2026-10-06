// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include "core/uwp/guest_startup.h"

namespace Core::Uwp {
// A closed, branch-free CRT prefix, NOT a general x86 decoder or CPU emulator.
// The original instructions are executed on the CPU, without rewriting them.
// Its only memory operand reads argc from a host-validated EntryParams pointer.
inline constexpr std::array<std::uint8_t, 20> NativeCrtPrefix{
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50,
    0x44, 0x8b, 0x37, 0x48, 0x89, 0xf3, 0x4c, 0x8d, 0x7f, 0x08};

struct NativeEntryPlan {
    bool valid{};
    std::uint64_t entry_offset{}, plt_offset{}, slot_offset{};
    std::uint32_t symbol{};
    GuestImportKey import;
    std::string error;
};

// Certify the WHOLE reachable guest path before permitting RX: nine fixed
// instructions, a direct CALL, a six-byte RIP-relative PLT JMP, then an owned
// diagnostic stop thunk. No loop, FS/GS access, arbitrary call or RET is allowed.
[[nodiscard]] inline NativeEntryPlan PlanNativeEntry(const GuestPayload& payload,
                                                     const GuestLinkManifest& manifest) {
    NativeEntryPlan out;
    const auto fail = [&](const char* error) {
        out.error = error;
        return out;
    };
    if (!payload.Ready() || !manifest.valid || StartupPageFlags(payload.image.plan).empty())
        return fail("native_entry_payload_invalid");
    const auto& plan = payload.image.plan;
    const auto& bytes = payload.image.bytes;
    const auto contains = [&](std::uint64_t offset, std::uint64_t size, bool executable) {
        if (offset > bytes.size() || size > bytes.size() - offset)
            return false;
        for (const auto& segment : plan.segments) {
            const auto start = segment.address - plan.base;
            const auto available = executable ? segment.file_size : segment.memory_size;
            if (offset < start || offset - start > available || size > available - (offset - start))
                continue;
            return executable ? (segment.flags & 1) != 0
                              : (segment.flags & 1) == 0 &&
                                    ((segment.flags & 2) != 0 || segment.type == 0x61000010);
        }
        return false;
    };
    out.entry_offset = plan.entry_offset;
    if (!contains(out.entry_offset, 25, true) ||
        !std::equal(NativeCrtPrefix.begin(), NativeCrtPrefix.end(),
                    bytes.begin() + static_cast<std::size_t>(out.entry_offset)) ||
        bytes[static_cast<std::size_t>(out.entry_offset) + 20] != 0xe8)
        return fail("native_entry_prefix_unsupported");
    const auto displacement = [&](std::uint64_t offset) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= std::uint64_t{bytes[static_cast<std::size_t>(offset) + i]} << (8 * i);
        return value & 0x80000000ULL ? value | 0xffffffff00000000ULL : value;
    };
    if (!AddGuestSigned(out.entry_offset + 25, displacement(out.entry_offset + 21),
                        out.plt_offset) ||
        !contains(out.plt_offset, 6, true) ||
        bytes[static_cast<std::size_t>(out.plt_offset)] != 0xff ||
        bytes[static_cast<std::size_t>(out.plt_offset) + 1] != 0x25)
        return fail("native_entry_first_call_not_import_plt");
    if ((out.entry_offset & 4095) > 4096 - 25 || (out.plt_offset & 4095) > 4096 - 6 ||
        (out.entry_offset < out.plt_offset + 6 && out.plt_offset < out.entry_offset + 25))
        return fail("native_entry_code_crosses_page_or_overlaps");
    if (!AddGuestSigned(out.plt_offset + 6, displacement(out.plt_offset + 2), out.slot_offset) ||
        out.slot_offset % 8 || !contains(out.slot_offset, 8, false))
        return fail("native_entry_import_slot_invalid");
    const auto imports = ResolveGuestImports(manifest);
    if (!imports.valid)
        return fail("native_entry_import_namespace_invalid");
    unsigned matches = 0;
    for (const auto& relocation : manifest.relocation_records) {
        if (relocation.address != plan.base + out.slot_offset)
            continue;
        if (relocation.type != 7 || relocation.addend_bits ||
            relocation.symbol >= manifest.symbol_records.size())
            return fail("native_entry_import_relocation_invalid");
        const auto& symbol = manifest.symbol_records[relocation.symbol];
        if (symbol.section || symbol.type != 2)
            return fail("native_entry_import_not_undefined_function");
        out.symbol = relocation.symbol;
        ++matches;
    }
    if (matches != 1)
        return fail("native_entry_import_relocation_missing_or_duplicate");
    for (const auto& binding : imports.bindings) {
        if (binding.symbol == out.symbol) {
            out.import = binding.key;
            out.valid = true;
            return out;
        }
    }
    return fail("native_entry_import_binding_missing");
}

// The stop thunk records registers without invoking guest services or the CRT.
// Host RSP is kept outside the guest image; it is restored only by owned code.
struct NativeEntryCapture {
    std::uint64_t saved_host_rsp{}, reached{}, guest_rsp{}, return_ip{};
    std::array<std::uint64_t, 6> arguments{};
    std::uint64_t argc{}, argv{}, exit_function{};
};
static_assert(sizeof(NativeEntryCapture) == 104);

struct NativeEntryBridge {
    std::vector<std::uint8_t> code;
    std::size_t resume_offset{}, stop_offset{};
};

// Win64: void(params*, entry, capture*). All eight nonvolatile GPRs and all ten
// nonvolatile XMM registers are saved even though this particular prefix only
// changes some of them. Guest main is entered via JMP, not CALL; as in the
// upstream RunMainEntry, two qwords of EntryParams seed RSP and RSP%16 == 8.
// A separate owned stack remains parameter backing; execution uses worker RSP.
[[nodiscard]] inline NativeEntryBridge MakeNativeEntryBridge(std::uint64_t capture,
                                                             std::uint64_t stop) {
    NativeEntryBridge out;
    auto& code = out.code;
    const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
        code.insert(code.end(), bytes);
    };
    const auto immediate = [&](std::uint64_t value, unsigned size = 8) {
        for (unsigned i = 0; i < size; ++i)
            code.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    };
    emit({0x53, 0x55, 0x57, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
    emit({0x48, 0x81, 0xec});
    immediate(216, 4); // Stack aligned after eight pushes; 32-byte shadow space.
    const auto xmm = [&](bool load) {
        for (unsigned reg = 6; reg < 16; ++reg) {
            emit({0xf3});
            if (reg >= 8)
                emit({0x44});
            emit({0x0f, static_cast<std::uint8_t>(load ? 0x6f : 0x7f),
                  static_cast<std::uint8_t>(0x84 | ((reg & 7) << 3)), 0x24});
            immediate(48 + (reg - 6) * 16, 4);
        }
    };
    xmm(false);
    emit({0x49, 0xbb}); // R11 = capture (owned immediate, never supplied by guest).
    immediate(capture);
    emit({0x49, 0x89, 0x23, 0x48, 0x89, 0xcf, 0x49, 0x89, 0xd2});
    // saved RSP; RDI=params; R10=entry; RSI=non-returning diagnostic exit.
    emit({0x48, 0xbe});
    immediate(stop);
    emit({0x48, 0x83, 0xec, 0x08, 0xff, 0x71, 0x08, 0xff, 0x31, 0x31, 0xc0, 0x31, 0xc9, 0x31,
          0xd2, 0x45, 0x31, 0xc0, 0x45, 0x31, 0xc9, 0x45, 0x31, 0xdb, 0xfc, 0x41, 0xff, 0xe2});
    out.resume_offset = code.size();
    xmm(true);
    emit({0xfc, 0x48, 0x81, 0xc4});
    immediate(216, 4);
    emit({0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5e, 0x5f, 0x5d, 0x5b, 0xc3});
    out.stop_offset = code.size();
    emit({0x49, 0xbb});
    immediate(capture);
    // Record the six SysV argument registers, stack and actual CALL return PC.
    emit({0x49, 0x89, 0x7b, 0x20, 0x49, 0x89, 0x73, 0x28, 0x49, 0x89, 0x53, 0x30,
          0x49, 0x89, 0x4b, 0x38, 0x4d, 0x89, 0x43, 0x40, 0x4d, 0x89, 0x4b, 0x48,
          0x49, 0x89, 0x63, 0x10, 0x48, 0x8b, 0x04, 0x24, 0x49, 0x89, 0x43, 0x18,
          0x4d, 0x89, 0x73, 0x50, 0x4d, 0x89, 0x7b, 0x58, 0x49, 0x89, 0x5b, 0x60,
          0x49, 0xc7, 0x43, 0x08, 1,    0,    0,    0,    0x49, 0x8b, 0x23, 0xe9});
    // Jump back to the restoration epilogue, never RET into unimplemented CRT.
    const auto relative = static_cast<std::uint32_t>(
        out.resume_offset - (code.size() + 4)); // x86 rel32 two's-complement.
    immediate(relative, 4);
    return out;
}
} // namespace Core::Uwp
