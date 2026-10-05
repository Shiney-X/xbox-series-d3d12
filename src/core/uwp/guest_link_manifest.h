// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <map>
#include "core/uwp/guest_payload.h"

namespace Core::Uwp {
struct GuestLinkManifest {
    bool valid{};
    std::string error;
    std::uint64_t dynamic_entries{}, symbols{}, relocations{}, relative{}, jump_slots{},
        symbol_relocations{}, tls_relocations{}, other_relocations{}, target_checks{},
        undefined_functions{}, undefined_objects{}, undefined_other{}, tls_segments{},
        tls_address{}, tls_file_bytes{}, tls_memory_bytes{}, tls_alignment{};
    std::vector<std::string> needed, modules, libraries, imports;

    [[nodiscard]] std::string Details() const {
        const auto names = [](const std::vector<std::string>& list) {
            std::string value;
            for (const auto& name : list) {
                if (!value.empty())
                    value += ',';
                value += name;
            }
            return value;
        };
        return "stage=link_manifest;guest_executed=0;game_frame=0;loader_linked=0;"
               "imports_resolved=0;relocations_applied=0;ready_for_boot=0;manifest_valid=" +
               std::to_string(valid) + ";dynamic_entries=" + std::to_string(dynamic_entries) +
               ";symbols=" + std::to_string(symbols) +
               ";undefined_functions=" + std::to_string(undefined_functions) +
               ";undefined_objects=" + std::to_string(undefined_objects) +
               ";undefined_other=" + std::to_string(undefined_other) +
               ";relocations=" + std::to_string(relocations) +
               ";relative_relocations=" + std::to_string(relative) +
               ";jump_slots=" + std::to_string(jump_slots) +
               ";symbol_relocations=" + std::to_string(symbol_relocations) +
               ";tls_relocations=" + std::to_string(tls_relocations) +
               ";other_relocations=" + std::to_string(other_relocations) +
               ";relocation_target_checks=" + std::to_string(target_checks) +
               ";target_scope=known_widths_only;tls_segments=" + std::to_string(tls_segments) +
               ";tls_address=" + std::to_string(tls_address) +
               ";tls_file_bytes=" + std::to_string(tls_file_bytes) +
               ";tls_memory_bytes=" + std::to_string(tls_memory_bytes) +
               ";tls_alignment=" + std::to_string(tls_alignment) + ";needed=" + names(needed) +
               ";modules=" + names(modules) + ";libraries=" + names(libraries) +
               ";imports=" + names(imports) + ";error=" + error;
    }
};

// Orbis SCE dynamic-table profile only, based on Module::LoadDynamicInfo.
// Resolve offsets in the ELF logical file domain before reading SELF bytes.
// This inventories link requirements; it neither links nor executes anything.
[[nodiscard]] inline GuestLinkManifest InspectGuestLinkManifest(
    std::span<const std::uint8_t> file) {
    GuestLinkManifest result;
    const auto fail = [&](const char* error) {
        result.error = error;
        return result;
    };
    const auto layout = PlanGuestPayloadFile(file);
    if (!layout.image.plan.valid || !layout.image.error.empty())
        return fail("payload_layout_invalid");
    const auto prefix = file.first(std::min<std::size_t>(file.size(), GuestPrefixLimit));
    const auto header = InspectGuestPrefix(prefix, file.size());
    if (!header.orbis_identity)
        return fail("requires_orbis_dynamic_profile");
    const auto read = [](std::span<const std::uint8_t> bytes, std::size_t offset, unsigned count) {
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= std::uint64_t(bytes[offset + i]) << (8 * i);
        return value;
    };
    const auto logical_slice = [&](std::uint64_t offset, std::uint64_t size) {
        std::span<const std::uint8_t> found;
        unsigned matches = 0;
        for (const auto& range : layout.file_ranges) {
            if (offset < range.logical_offset || offset - range.logical_offset > range.size ||
                size > range.size - (offset - range.logical_offset))
                continue;
            const auto physical = range.physical_offset + (offset - range.logical_offset);
            if (physical > file.size() || size > file.size() - physical)
                continue;
            found =
                file.subspan(static_cast<std::size_t>(physical), static_cast<std::size_t>(size));
            ++matches;
        }
        return matches == 1 ? found : std::span<const std::uint8_t>{};
    };
    const auto mapped = [&](std::uint64_t address, std::uint64_t size) {
        for (const auto& segment : layout.image.plan.segments)
            if (address >= segment.address && address - segment.address <= segment.memory_size &&
                size <= segment.memory_size - (address - segment.address))
                return true;
        return false;
    };
    const auto phoff = read(prefix, static_cast<std::size_t>(header.elf_offset) + 32, 8);
    std::span<const std::uint8_t> dynamic, dynlib;
    bool have_dynamic = false, have_dynlib = false;
    for (std::size_t i = 0; i < header.program_headers; ++i) {
        const auto ph = static_cast<std::size_t>(header.elf_offset + phoff + i * 56);
        const auto type = read(prefix, ph, 4);
        const auto offset = read(prefix, ph + 8, 8), address = read(prefix, ph + 16, 8);
        const auto size = read(prefix, ph + 32, 8), memory = read(prefix, ph + 40, 8);
        if (type == 2 || type == 0x61000000) {
            auto& present = type == 2 ? have_dynamic : have_dynlib;
            if (present)
                return fail("duplicate_dynamic_program_header");
            present = true;
            if (size == 0 || size > (type == 2 ? 65536ULL : 8 * 1024 * 1024ULL))
                return fail("dynamic_data_budget_or_size");
            auto& bytes = type == 2 ? dynamic : dynlib;
            bytes = logical_slice(offset, size);
            if (bytes.size() != size)
                return fail("dynamic_data_unmapped");
        } else if (type == 7) {
            if (++result.tls_segments > 1)
                return fail("multiple_tls_segments_unsupported");
            const auto alignment = read(prefix, ph + 48, 8);
            if (size > memory || memory > GuestImageLimit ||
                (alignment > 1 && ((alignment & (alignment - 1)) || alignment > 65536)))
                return fail("invalid_tls_layout");
            if ((memory && !mapped(address, memory)) ||
                (size && logical_slice(offset, size).size() != size))
                return fail("tls_image_unmapped");
            result.tls_address = address;
            result.tls_file_bytes = size;
            result.tls_memory_bytes = memory;
            result.tls_alignment = alignment;
        }
    }
    if (!have_dynamic || !have_dynlib || dynamic.size() % 16)
        return fail("missing_or_invalid_orbis_dynamic_tables");
    std::map<std::uint64_t, std::uint64_t> tags;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> names;
    bool terminated = false;
    for (std::size_t offset = 0; offset < dynamic.size(); offset += 16) {
        const auto tag = read(dynamic, offset, 8), value = read(dynamic, offset + 8, 8);
        if (tag == 0) {
            terminated = true;
            break;
        }
        ++result.dynamic_entries;
        if (tag == 1 || tag == 0x6100000f || tag == 0x61000015) {
            if (names.size() >= 512)
                return fail("dependency_budget_exceeded");
            names.emplace_back(tag, value);
        } else if (tag >= 0x61000029 && tag <= 0x6100003f && (tag & 1)) {
            if (!tags.emplace(tag, value).second)
                return fail("duplicate_link_table_tag");
        }
    }
    if (!terminated)
        return fail("unterminated_dynamic_table");
    const auto table = [&](std::uint64_t offset_tag, std::uint64_t size_tag) {
        if (!tags.contains(offset_tag) || !tags.contains(size_tag))
            return std::span<const std::uint8_t>{};
        const auto offset = tags.at(offset_tag), size = tags.at(size_tag);
        if (offset > dynlib.size() || size > dynlib.size() - offset)
            return std::span<const std::uint8_t>{};
        return dynlib.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
    };
    const auto strings = table(0x61000035, 0x61000037);
    const auto symbols = table(0x61000039, 0x6100003f);
    if (strings.empty() || strings.size() > 4 * 1024 * 1024 || symbols.empty() ||
        symbols.size() % 24 || symbols.size() / 24 > 131072 || !tags.contains(0x6100003b) ||
        tags.at(0x6100003b) != 24)
        return fail("invalid_or_missing_string_symbol_tables");
    std::size_t name_bytes = 0;
    const auto string_at = [&](std::uint64_t offset, std::string& output) {
        if (offset >= strings.size())
            return false;
        for (std::size_t i = static_cast<std::size_t>(offset); i < strings.size(); ++i) {
            const auto byte = strings[i];
            if (byte == 0) {
                name_bytes += output.size();
                return !output.empty() && name_bytes <= 1024 * 1024;
            }
            if (output.size() >= 256 || byte < 32 || byte > 126 || byte == ';' || byte == ',')
                return false;
            output += static_cast<char>(byte);
        }
        return false;
    };
    for (const auto& [tag, value] : names) {
        std::string name;
        if (!string_at(tag == 1 ? value : value & 0xffffffffULL, name))
            return fail("invalid_dependency_name");
        (tag == 1            ? result.needed
         : tag == 0x6100000f ? result.modules
                             : result.libraries)
            .push_back(name);
    }
    result.symbols = symbols.size() / 24;
    for (std::size_t offset = 0; offset < symbols.size(); offset += 24) {
        const auto bind = symbols[offset + 4] >> 4, type = symbols[offset + 4] & 15;
        if (read(symbols, offset + 6, 2) != 0 || (bind != 1 && bind != 2))
            continue;
        if (result.imports.size() >= 8192)
            return fail("import_budget_exceeded");
        std::string name;
        if (!string_at(read(symbols, offset, 4), name))
            return fail("invalid_import_name");
        result.imports.push_back(name);
        if (type == 2)
            ++result.undefined_functions;
        else if (type == 1)
            ++result.undefined_objects;
        else
            ++result.undefined_other;
    }
    for (const auto jump_table : {false, true}) {
        const std::uint64_t offset_tag = jump_table ? 0x61000029 : 0x6100002f;
        const std::uint64_t size_tag = jump_table ? 0x6100002d : 0x61000031;
        if (!tags.contains(offset_tag) && !tags.contains(size_tag))
            continue;
        const auto bytes = table(offset_tag, size_tag);
        if (!tags.contains(offset_tag) || !tags.contains(size_tag) ||
            tags.at(offset_tag) > dynlib.size() ||
            tags.at(size_tag) > dynlib.size() - tags.at(offset_tag) ||
            bytes.size() != tags.at(size_tag) || bytes.size() % 24 || bytes.size() / 24 > 262144 ||
            (jump_table && (!tags.contains(0x6100002b) || tags.at(0x6100002b) != 7)) ||
            (!jump_table && (!tags.contains(0x61000033) || tags.at(0x61000033) != 24)))
            return fail("invalid_relocation_table_layout");
        for (std::size_t offset = 0; offset < bytes.size(); offset += 24) {
            const auto address = read(bytes, offset, 8), info = read(bytes, offset + 8, 8);
            const auto type = static_cast<std::uint32_t>(info);
            const auto symbol = info >> 32;
            if (symbol >= result.symbols || (type == 8 && symbol != 0))
                return fail("invalid_relocation_symbol");
            ++result.relocations;
            std::uint64_t width = 0;
            if (type == 8) {
                ++result.relative;
                width = 8;
            } else if (type == 7) {
                ++result.jump_slots;
                width = 8;
            } else if (type == 1 || type == 6 || type == 2 || type == 10 || type == 11) {
                ++result.symbol_relocations;
                width = type == 2 || type == 10 || type == 11 ? 4 : 8;
            } else if (type == 16 || type == 17 || type == 18 || type == 36) {
                ++result.tls_relocations;
                width = type == 36 ? 16 : 8;
            } else if (type != 0) {
                ++result.other_relocations;
            }
            if (width) {
                if (!mapped(address, width))
                    return fail("relocation_target_outside_mapped_segment");
                ++result.target_checks;
            }
        }
    }
    result.valid = true;
    return result;
}
} // namespace Core::Uwp
