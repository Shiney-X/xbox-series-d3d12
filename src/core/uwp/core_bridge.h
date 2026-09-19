// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace Core::Uwp {

struct BridgeStatus {
    bool initialized{};
    bool psf_abi_valid{};
    bool endian_valid{};
    const char* upstream_version{};

    [[nodiscard]] bool AllPassed() const noexcept {
        return initialized && psf_abi_valid && endian_valid;
    }

    [[nodiscard]] std::string SerializeDetails() const;
};

[[nodiscard]] BridgeStatus InitializeBridge() noexcept;

} // namespace Core::Uwp
