// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "core/uwp/core_bridge.h"

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
