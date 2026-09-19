// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string_view>
#include <vector>

#include "core/file_format/psf.h"
#include "core/uwp/core_bridge.h"

namespace {

std::vector<std::uint8_t> CreateMetadataPsf() {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 3> values{{
        {"TITLE", "Sonic Mania"},
        {"TITLE_ID", "CUSA07023"},
        {"APP_VER", "01.04"},
    }};

    const std::size_t index_size = values.size() * sizeof(PSFRawEntry);
    std::vector<std::uint8_t> bytes(sizeof(PSFHeader) + index_size);
    const std::size_t key_table_offset = bytes.size();
    std::array<std::uint16_t, values.size()> key_offsets{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        key_offsets[index] = static_cast<std::uint16_t>(bytes.size() - key_table_offset);
        bytes.insert(bytes.end(), values[index].first.begin(), values[index].first.end());
        bytes.push_back(0U);
    }
    const std::size_t data_table_offset = bytes.size();

    std::array<PSFRawEntry, values.size()> entries{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        entries[index].key_offset = key_offsets[index];
        entries[index].param_fmt.FromRaw(static_cast<u16>(PSFEntryFmt::Text));
        entries[index].param_len = static_cast<u32>(values[index].second.size() + 1U);
        entries[index].param_max_len = entries[index].param_len;
        entries[index].data_offset = static_cast<u32>(bytes.size() - data_table_offset);
        bytes.insert(bytes.end(), values[index].second.begin(), values[index].second.end());
        bytes.push_back(0U);
    }

    PSFHeader header{};
    header.magic = PSF_MAGIC;
    header.version = PSF_VERSION_1_1;
    header.key_table_offset = static_cast<u32>(key_table_offset);
    header.data_table_offset = static_cast<u32>(data_table_offset);
    header.index_table_entries = static_cast<u32>(entries.size());
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), entries.data(), index_size);
    return bytes;
}

} // namespace

TEST(UwpCoreBridge, InitializesAgainstTheUpstreamAbi) {
    const Core::Uwp::BridgeStatus status = Core::Uwp::InitializeBridge();

    EXPECT_TRUE(status.initialized);
    EXPECT_TRUE(status.endian_valid);
    EXPECT_TRUE(status.psf_abi_valid);
    EXPECT_TRUE(status.AllPassed());
    EXPECT_STREQ(status.upstream_version, "v0.18.0");
}

TEST(UwpCoreBridge, ExposesDiagnosticDetails) {
    const std::string details = Core::Uwp::InitializeBridge().SerializeDetails();

    EXPECT_NE(details.find("upstream=v0.18.0"), std::string::npos);
    EXPECT_NE(details.find("initialized=1"), std::string::npos);
    EXPECT_NE(details.find("psf_abi=1"), std::string::npos);
    EXPECT_NE(details.find("endian=1"), std::string::npos);
}

TEST(UwpCoreBridge, ParsesGameMetadataFromUpstreamPsfStructures) {
    const Core::Uwp::GameMetadata metadata = Core::Uwp::ParseParamSfo(CreateMetadataPsf());

    ASSERT_TRUE(metadata.valid) << metadata.error;
    EXPECT_EQ(metadata.title, "Sonic Mania");
    EXPECT_EQ(metadata.title_id, "CUSA07023");
    EXPECT_EQ(metadata.app_version, "01.04");
    EXPECT_TRUE(metadata.error.empty());
}

TEST(UwpCoreBridge, RejectsTruncatedPsfWithoutReadingOutOfBounds) {
    std::vector<std::uint8_t> bytes = CreateMetadataPsf();
    bytes.resize(sizeof(PSFHeader) + 1U);

    const Core::Uwp::GameMetadata metadata = Core::Uwp::ParseParamSfo(bytes);

    EXPECT_FALSE(metadata.valid);
    EXPECT_EQ(metadata.error, "truncated_index");
}

TEST(UwpCoreBridge, RejectsInvalidPsfMagic) {
    std::vector<std::uint8_t> bytes = CreateMetadataPsf();
    bytes.front() ^= 0xFFU;

    const Core::Uwp::GameMetadata metadata = Core::Uwp::ParseParamSfo(bytes);

    EXPECT_FALSE(metadata.valid);
    EXPECT_EQ(metadata.error, "invalid_magic");
}
