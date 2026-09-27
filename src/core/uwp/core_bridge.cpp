// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/uwp/core_bridge.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>

#include "common/endian.h"
#include "core/file_format/psf.h"

namespace Core::Uwp {

namespace {

constexpr char UpstreamVersion[] = "v0.18.0";

bool ValidateEndianTypes() noexcept {
    u32_le little{};
    u32_be big{};
    little = 0x12345678U;
    big = 0x12345678U;
    return little == 0x12345678U && big == 0x12345678U;
}

bool ValidatePsfAbi() noexcept {
    PSFHeader header{};
    header.magic = PSF_MAGIC;
    header.version = PSF_VERSION_1_1;
    header.index_table_entries = 0;
    return sizeof(PSFHeader) == 0x14 && sizeof(PSFRawEntry) == 0x10 && header.magic == PSF_MAGIC &&
           header.version == PSF_VERSION_1_1;
}

bool RangeIsValid(std::size_t offset, std::size_t size, std::size_t total) noexcept {
    return offset <= total && size <= total - offset;
}

std::optional<std::string_view> ReadNullTerminatedString(std::span<const std::uint8_t> bytes,
                                                         std::size_t offset,
                                                         std::size_t maximum_size) noexcept {
    if (!RangeIsValid(offset, maximum_size, bytes.size())) {
        return {};
    }
    const char* begin = reinterpret_cast<const char*>(bytes.data() + offset);
    const void* terminator = std::memchr(begin, '\0', maximum_size);
    if (terminator == nullptr) {
        return {};
    }
    return std::string_view{begin,
                            static_cast<std::size_t>(static_cast<const char*>(terminator) - begin)};
}

} // namespace

BridgeStatus InitializeBridge() noexcept {
    BridgeStatus status{};
    status.upstream_version = UpstreamVersion;
    status.endian_valid = ValidateEndianTypes();
    status.psf_abi_valid = ValidatePsfAbi();
    status.initialized = true;
    return status;
}

std::string BridgeStatus::SerializeDetails() const {
    std::ostringstream output;
    output << "upstream=" << upstream_version << ";initialized=" << initialized
           << ";psf_abi=" << psf_abi_valid << ";endian=" << endian_valid;
    return output.str();
}

GameMetadata ParseParamSfo(std::span<const std::uint8_t> bytes) noexcept {
    GameMetadata metadata{};
    try {
        if (bytes.size() < sizeof(PSFHeader)) {
            metadata.error = "truncated_header";
            return metadata;
        }

        PSFHeader header{};
        std::memcpy(&header, bytes.data(), sizeof(header));
        if (header.magic != PSF_MAGIC) {
            metadata.error = "invalid_magic";
            return metadata;
        }
        if (header.version != PSF_VERSION_1_0 && header.version != PSF_VERSION_1_1) {
            metadata.error = "unsupported_version";
            return metadata;
        }

        const std::size_t entry_count = header.index_table_entries;
        constexpr std::size_t maximum_entries = 4096U;
        if (entry_count > maximum_entries ||
            entry_count > (std::numeric_limits<std::size_t>::max() - sizeof(PSFHeader)) /
                              sizeof(PSFRawEntry)) {
            metadata.error = "invalid_entry_count";
            return metadata;
        }
        const std::size_t index_size = entry_count * sizeof(PSFRawEntry);
        if (!RangeIsValid(sizeof(PSFHeader), index_size, bytes.size())) {
            metadata.error = "truncated_index";
            return metadata;
        }

        const std::size_t key_table_offset = header.key_table_offset;
        const std::size_t data_table_offset = header.data_table_offset;
        const std::size_t minimum_key_table_offset = sizeof(PSFHeader) + index_size;
        if (key_table_offset < minimum_key_table_offset || key_table_offset > bytes.size() ||
            data_table_offset > bytes.size() || key_table_offset > data_table_offset) {
            metadata.error = "invalid_table_offset";
            return metadata;
        }

        for (std::size_t index = 0; index < entry_count; ++index) {
            PSFRawEntry entry{};
            const std::size_t entry_offset = sizeof(PSFHeader) + index * sizeof(PSFRawEntry);
            std::memcpy(&entry, bytes.data() + entry_offset, sizeof(entry));

            const std::size_t key_offset = key_table_offset + entry.key_offset;
            if (key_offset < key_table_offset || key_offset >= data_table_offset) {
                metadata.error = "invalid_key_offset";
                return metadata;
            }
            const auto key =
                ReadNullTerminatedString(bytes, key_offset, data_table_offset - key_offset);
            if (!key.has_value()) {
                metadata.error = "unterminated_key";
                return metadata;
            }

            if (entry.param_fmt.Raw() != static_cast<u16>(PSFEntryFmt::Text)) {
                continue;
            }
            const std::size_t value_offset = data_table_offset + entry.data_offset;
            const std::size_t value_size = entry.param_len;
            if (value_offset < data_table_offset || value_size == 0U ||
                !RangeIsValid(value_offset, value_size, bytes.size())) {
                metadata.error = "invalid_value_range";
                return metadata;
            }
            const auto value = ReadNullTerminatedString(bytes, value_offset, value_size);
            if (!value.has_value()) {
                metadata.error = "unterminated_value";
                return metadata;
            }

            if (*key == "TITLE") {
                metadata.title = *value;
            } else if (*key == "TITLE_ID") {
                metadata.title_id = *value;
            } else if (*key == "APP_VER") {
                metadata.app_version = *value;
            }
        }

        if (metadata.title.empty() || metadata.title_id.empty()) {
            metadata.error = "missing_required_metadata";
            return metadata;
        }
        if (metadata.app_version.empty()) {
            metadata.app_version = "unknown";
        }
        metadata.valid = true;
        return metadata;
    } catch (...) {
        metadata = {};
        metadata.error = "parser_exception";
        return metadata;
    }
}

} // namespace Core::Uwp
