// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <compare>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "core/uwp/guest_link_manifest.h"

namespace Core::Uwp {
inline constexpr std::string_view GuestIdAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";

[[nodiscard]] inline std::string EncodeGuestId(std::uint16_t id) {
    std::string encoded;
    if (id >= 4096)
        encoded += GuestIdAlphabet[(id >> 12) & 63];
    if (id >= 64)
        encoded += GuestIdAlphabet[(id >> 6) & 63];
    encoded += GuestIdAlphabet[id & 63];
    return encoded;
}
[[nodiscard]] inline bool DecodeGuestId(std::string_view encoded, std::uint16_t& id) {
    if (encoded.empty() || encoded.size() > 3)
        return false;
    std::uint32_t value = 0;
    for (const char c : encoded) {
        const auto digit = GuestIdAlphabet.find(c);
        if (digit == std::string_view::npos)
            return false;
        value = value * 64 + static_cast<std::uint32_t>(digit);
    }
    if (value > UINT16_MAX || EncodeGuestId(static_cast<std::uint16_t>(value)) != encoded)
        return false;
    id = static_cast<std::uint16_t>(value);
    return true;
}

struct GuestImportKey {
    std::string nid, library, module;
    std::uint16_t library_version{};
    std::uint8_t module_major{}, module_minor{}, type{};
    auto operator<=>(const GuestImportKey&) const = default;

    [[nodiscard]] std::string Label() const {
        return nid + "|" + library + "|" + std::to_string(library_version) + "|" + module + "|" +
               std::to_string(module_major) + "." + std::to_string(module_minor) + "|" +
               std::to_string(type);
    }
};

// Data-only numeric export. NOT an HLE callback, callable host pointer or
// automatic stub. Production scans register none until runtime wiring exists.
struct GuestDataExport {
    GuestImportKey key;
    std::uint64_t numeric_address{};
};
struct GuestImportBinding {
    std::uint32_t symbol{};
    std::uint16_t library_id{}, module_id{};
    GuestImportKey key;
    std::uint64_t numeric_address{};
};
struct GuestImportPlan {
    bool valid{};
    std::string error;
    std::uint64_t exports{}, matched{}, unresolved{};
    std::vector<GuestImportBinding> bindings;
    std::vector<std::uint64_t> addresses_by_symbol;

    [[nodiscard]] std::string Details() const {
        std::string keys;
        for (const auto& binding : bindings) {
            if (!keys.empty())
                keys += ',';
            keys += binding.key.Label();
        }
        return "stage=import_resolution;guest_executed=0;game_frame=0;loader_linked=0;"
               "imports_resolved=0;runtime_exports_callable=0;ready_for_boot=0;"
               "export_scope=numeric_data_only;namespace_valid=" +
               std::to_string(valid) + ";normalized_imports=" + std::to_string(bindings.size()) +
               ";registered_data_exports=" + std::to_string(exports) +
               ";numeric_imports_matched=" + std::to_string(matched) +
               ";unresolved_imports=" + std::to_string(unresolved) + ";binding_keys=" + keys +
               ";error=" + error;
    }
};

[[nodiscard]] inline bool ValidGuestKeyPart(std::string_view text) {
    return !text.empty() && text.size() <= 256 &&
           std::all_of(text.begin(), text.end(), [](unsigned char c) {
               return c >= 33 && c <= 126 && c != '#' && c != '|' && c != ';' && c != ',';
           });
}

// Exact typed/versioned lookup, based on ModuleInfo/LibraryInfo/EncodeId and
// Linker::Resolve, deliberately without AeroLib's fallback stubs. Module
// major/minor are also matched strictly (stricter than the upstream HLE key).
[[nodiscard]] inline GuestImportPlan ResolveGuestImports(
    const GuestLinkManifest& manifest, std::span<const GuestDataExport> exports = {}) {
    GuestImportPlan result;
    const auto fail = [&](const char* error) {
        result.error = error;
        result.matched = 0;
        result.unresolved = 0;
        result.bindings.clear();
        result.addresses_by_symbol.clear();
        return result;
    };
    if (!manifest.valid || manifest.symbols > 131072 ||
        manifest.symbol_records.size() != manifest.symbols || manifest.imports.size() > 8192 ||
        manifest.import_symbol_indices.size() != manifest.imports.size() ||
        manifest.module_records.size() != manifest.modules.size() ||
        manifest.library_records.size() != manifest.libraries.size() ||
        manifest.module_records.size() + manifest.library_records.size() > 512)
        return fail("manifest_records_unavailable_or_budget");
    if (exports.size() > 4096)
        return fail("export_registry_budget");
    std::map<GuestImportKey, std::uint64_t> registry;
    for (const auto& entry : exports) {
        if (!ValidGuestKeyPart(entry.key.nid) || !ValidGuestKeyPart(entry.key.library) ||
            !ValidGuestKeyPart(entry.key.module) || entry.key.type > 2 || !entry.numeric_address)
            return fail("invalid_data_export");
        if (!registry.emplace(entry.key, entry.numeric_address).second)
            return fail("duplicate_export_key");
    }
    result.exports = registry.size();
    std::map<std::uint16_t, const GuestLinkDependency*> libraries, modules;
    for (const auto& entry : manifest.module_records) {
        if (!ValidGuestKeyPart(entry.name))
            return fail("invalid_module_name");
        if (!modules.emplace(static_cast<std::uint16_t>(entry.packed >> 48), &entry).second)
            return fail("duplicate_module_id");
    }
    for (const auto& entry : manifest.library_records) {
        if (!ValidGuestKeyPart(entry.name))
            return fail("invalid_library_name");
        if (!libraries.emplace(static_cast<std::uint16_t>(entry.packed >> 48), &entry).second)
            return fail("duplicate_library_id");
    }
    result.addresses_by_symbol.resize(static_cast<std::size_t>(manifest.symbols), 0);
    std::vector<bool> seen(static_cast<std::size_t>(manifest.symbols), false);
    for (std::size_t i = 0; i < manifest.imports.size(); ++i) {
        const auto symbol = manifest.import_symbol_indices[i];
        if (symbol >= manifest.symbols || seen[symbol])
            return fail("invalid_or_duplicate_import_symbol");
        seen[symbol] = true;
        const auto& info = manifest.symbol_records[symbol];
        if (info.section != 0 || (info.binding != 1 && info.binding != 2))
            return fail("invalid_import_symbol_binding");
        const std::string_view name = manifest.imports[i];
        const auto first = name.find('#');
        const auto second = first == std::string_view::npos ? first : name.find('#', first + 1);
        if (first == std::string_view::npos || second == std::string_view::npos ||
            name.find('#', second + 1) != std::string_view::npos ||
            !ValidGuestKeyPart(name.substr(0, first)))
            return fail("invalid_nid_namespace_shape");
        GuestImportBinding binding;
        binding.symbol = symbol;
        if (!DecodeGuestId(name.substr(first + 1, second - first - 1), binding.library_id) ||
            !DecodeGuestId(name.substr(second + 1), binding.module_id))
            return fail("invalid_encoded_import_id");
        if (!libraries.contains(binding.library_id))
            return fail("unknown_import_library_id");
        if (!modules.contains(binding.module_id))
            return fail("unknown_import_module_id");
        const auto& library = *libraries.at(binding.library_id);
        const auto& module = *modules.at(binding.module_id);
        binding.key = {std::string(name.substr(0, first)),
                       library.name,
                       module.name,
                       static_cast<std::uint16_t>(library.packed >> 32),
                       static_cast<std::uint8_t>(module.packed >> 40),
                       static_cast<std::uint8_t>(module.packed >> 32),
                       info.type};
        const auto found = registry.find(binding.key);
        if (found == registry.end()) {
            ++result.unresolved;
        } else {
            binding.numeric_address = found->second;
            result.addresses_by_symbol[symbol] = found->second;
            ++result.matched;
        }
        result.bindings.push_back(std::move(binding));
    }
    result.valid = true;
    return result;
}
} // namespace Core::Uwp
