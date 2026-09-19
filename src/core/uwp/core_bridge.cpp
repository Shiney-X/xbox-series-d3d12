// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/uwp/core_bridge.h"

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

} // namespace Core::Uwp
