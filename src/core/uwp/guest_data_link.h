// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/uwp/guest_import_resolver.h"

namespace Core::Uwp {
// A numeric diagnostic load bias, NOT a reservation or a host pointer.
inline constexpr std::uint64_t GuestDiagnosticLoadBias = 0x100000000ULL;

struct GuestDataLink {
    GuestPayload payload;
    GuestLinkManifest manifest;
    GuestImportPlan imports;
    bool valid{}, complete{}, writes_verified{}, untouched_verified{};
    std::uint64_t bias{}, relative_applied{}, local_applied{}, pending_imports{},
        pending_bindings{}, pending_types{}, none{}, checksum_after{}, data_import_applied{};
    std::string error;

    [[nodiscard]] std::string Details() const {
        return "stage=data_relocation;guest_executed=0;game_frame=0;loader_linked=0;"
               "imports_resolved=0;ready_for_boot=0;host_permissions_applied=0;"
               "address_scope=synthetic_load_bias;data_link_valid=" +
               std::to_string(valid) + ";load_bias=" + std::to_string(bias) +
               ";relocations_applied=" +
               std::to_string(relative_applied + local_applied + data_import_applied) +
               ";relative_applied=" + std::to_string(relative_applied) +
               ";local_symbol_applied=" + std::to_string(local_applied) +
               ";data_import_relocations_applied=" + std::to_string(data_import_applied) +
               ";numeric_imports_matched=" + std::to_string(imports.matched) +
               ";runtime_exports_callable=0" +
               ";pending_import_relocations=" + std::to_string(pending_imports) +
               ";pending_symbol_bindings=" + std::to_string(pending_bindings) +
               ";pending_type_relocations=" + std::to_string(pending_types) +
               ";none_relocations=" + std::to_string(none) +
               ";all_relocations_applied=" + std::to_string(complete) +
               ";writes_verified=" + std::to_string(writes_verified) +
               ";untouched_bytes_verified=" + std::to_string(untouched_verified) +
               ";checksum_before_fnv1a64=" + std::to_string(payload.checksum) +
               ";checksum_after_fnv1a64=" + std::to_string(checksum_after) +
               ";runtime_instance_created=0;error=" + error;
    }
};

// Checked mathematical B+A without signed overflow or implementation-defined
// uint64->int64 conversions. RELA's addend is encoded as two's complement.
[[nodiscard]] inline bool AddGuestSigned(std::uint64_t base, std::uint64_t bits,
                                         std::uint64_t& value) {
    if ((bits >> 63) != 0) {
        const auto magnitude = ~bits + 1;
        if (base < magnitude)
            return false;
        value = base - magnitude;
    } else {
        if (base > UINT64_MAX - bits)
            return false;
        value = base + bits;
    }
    return true;
}

// Own and revalidate both snapshot and staged image. No externally supplied
// mutable plan, symbol resolver, host callback, executable mapping or entry call.
// A valid PARTIAL result explicitly leaves external symbols/types untouched.
[[nodiscard]] inline GuestDataLink StageGuestDataLink(
    std::span<const std::uint8_t> file, std::uint64_t bias = GuestDiagnosticLoadBias,
    bool require_complete = false, std::span<const GuestDataExport> exports = {}) {
    GuestDataLink result;
    result.bias = bias;
    result.payload = StageGuestPayload(file);
    result.manifest = InspectGuestLinkManifest(file, true);
    const auto fail = [&](const char* error) {
        result.error = error;
        result.valid = result.complete = false;
    };
    if (!result.payload.Ready() || !result.manifest.valid) {
        fail("payload_or_manifest_invalid");
        return result;
    }
    result.imports = ResolveGuestImports(result.manifest, exports);
    if (!result.imports.valid) {
        fail("import_namespace_or_registry_invalid");
        return result;
    }
    const auto& plan = result.payload.image.plan;
    if (bias > UINT64_MAX - plan.base || bias + plan.base > UINT64_MAX - plan.image_size) {
        fail("biased_image_range_overflow");
        return result;
    }
    const auto mapped = [&](std::uint64_t address, std::uint64_t size, bool writable) {
        for (const auto& segment : plan.segments) {
            if (address < segment.address || address - segment.address > segment.memory_size ||
                size > segment.memory_size - (address - segment.address))
                continue;
            return !writable || ((segment.flags & 1U) == 0 &&
                                 ((segment.flags & 2U) != 0 || segment.type == 0x61000010));
        }
        return false;
    };
    struct Patch {
        std::size_t offset{};
        std::uint64_t value{}, original{};
    };
    struct Range {
        std::uint64_t start{}, end{};
    };
    std::vector<Patch> patches;
    std::vector<Range> ranges;
    std::uint64_t relative_count = 0, local_count = 0, import_count = 0;
    for (const auto& relocation : result.manifest.relocation_records) {
        const auto type = relocation.type;
        if (type == 0) {
            ++result.none;
            continue;
        }
        const std::uint64_t width = type == 36                                ? 16
                                    : (type == 2 || type == 10 || type == 11) ? 4
                                                                              : 8;
        if (type != 1 && type != 2 && type != 6 && type != 7 && type != 8 && type != 10 &&
            type != 11 && type != 16 && type != 17 && type != 18 && type != 36) {
            fail("unsupported_relocation_type");
            return result;
        }
        // Includes pending known-width writes, preventing conflicting targets
        // in RELA and JMPREL from being partially overwritten.
        ranges.push_back({relocation.address, relocation.address + width});
        std::uint64_t value = 0;
        if (type == 8) {
            if (!AddGuestSigned(bias, relocation.addend_bits, value)) {
                fail("relative_value_overflow");
                return result;
            }
            ++relative_count;
        } else if (type == 1 || type == 6 || type == 7) {
            const auto& symbol = result.manifest.symbol_records[relocation.symbol];
            if (symbol.section == 0) {
                const auto address = result.imports.addresses_by_symbol[relocation.symbol];
                if (!address) {
                    ++result.pending_imports;
                    continue; // Never replace missing imports with zero/success.
                }
                if (!AddGuestSigned(address, type == 1 ? relocation.addend_bits : 0, value)) {
                    fail("data_import_value_overflow");
                    return result;
                }
                ++import_count;
            } else {
                if (symbol.binding != 0 || symbol.section >= 0xff00 || symbol.type > 2) {
                    ++result.pending_bindings;
                    continue; // Globals/weak may require the module resolver.
                }
                if (!mapped(symbol.address, std::max<std::uint64_t>(1, symbol.size), false) ||
                    bias > UINT64_MAX - symbol.address) {
                    fail("local_symbol_outside_image_or_overflow");
                    return result;
                }
                const auto addend = type == 1 ? relocation.addend_bits : 0;
                if (!AddGuestSigned(bias + symbol.address, addend, value)) {
                    fail("local_symbol_value_overflow");
                    return result;
                }
                ++local_count;
            }
        } else {
            ++result.pending_types; // TLS and 32-bit/PC-relative require later gates.
            continue;
        }
        if (!mapped(relocation.address, 8, true)) {
            fail("selected_write_not_allowed");
            return result;
        }
        patches.push_back({static_cast<std::size_t>(relocation.address - plan.base), value, 0});
    }
    std::sort(ranges.begin(), ranges.end(),
              [](const Range& a, const Range& b) { return a.start < b.start; });
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].start < ranges[i - 1].end) {
            fail("overlapping_relocation_targets");
            return result;
        }
    }
    const auto pending = result.pending_imports + result.pending_bindings + result.pending_types;
    if (require_complete && pending != 0) {
        fail("pending_relocations_require_resolver");
        return result;
    }
    std::sort(patches.begin(), patches.end(),
              [](const Patch& a, const Patch& b) { return a.offset < b.offset; });
    auto& image = result.payload.image.bytes;
    const auto read = [&](std::size_t offset) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i)
            value |= std::uint64_t(image[offset + i]) << (i * 8);
        return value;
    };
    const auto write = [&](std::size_t offset, std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i)
            image[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
    };
    struct ExpectedRange {
        std::size_t start{}, end{}, physical{};
    };
    std::vector<ExpectedRange> expected;
    for (std::size_t i = 0; i < plan.segments.size(); ++i) {
        const auto& segment = plan.segments[i];
        const auto start = static_cast<std::size_t>(segment.address - plan.base);
        expected.push_back({start, start + static_cast<std::size_t>(segment.file_size),
                            static_cast<std::size_t>(result.payload.load_offsets[i])});
    }
    std::sort(expected.begin(), expected.end(),
              [](const ExpectedRange& a, const ExpectedRange& b) { return a.start < b.start; });
    const auto verify_untouched = [&] {
        std::size_t next = 0, backing = 0;
        for (std::size_t i = 0; i < image.size(); ++i) {
            if (next < patches.size() && i == patches[next].offset) {
                i += 7;
                ++next;
            } else {
                while (backing < expected.size() && expected[backing].end <= i)
                    ++backing;
                const auto byte =
                    backing < expected.size() && i >= expected[backing].start
                        ? file[expected[backing].physical + i - expected[backing].start]
                        : std::uint8_t{0};
                if (image[i] != byte)
                    return false;
            }
        }
        return true;
    };
    // All allocations and validation finish before the first write.
    for (auto& patch : patches) {
        patch.original = read(patch.offset);
        write(patch.offset, patch.value);
    }
    result.writes_verified = std::all_of(patches.begin(), patches.end(), [&](const Patch& patch) {
        return read(patch.offset) == patch.value;
    });
    result.untouched_verified = verify_untouched();
    if (!result.writes_verified || !result.untouched_verified) {
        for (const auto& patch : patches)
            write(patch.offset, patch.original);
        fail("post_write_verification_failed");
        return result;
    }
    result.relative_applied = relative_count;
    result.local_applied = local_count;
    result.data_import_applied = import_count;
    result.checksum_after = 14695981039346656037ULL;
    for (const auto byte : image)
        result.checksum_after = (result.checksum_after ^ byte) * 1099511628211ULL;
    result.complete = pending == 0;
    result.valid = true;
    return result;
}
} // namespace Core::Uwp
