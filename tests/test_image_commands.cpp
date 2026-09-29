// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>

#include <gtest/gtest.h>

#include "video_core/texture_cache/image_commands.h"

namespace {

TEST(ImageCommands, CopyRequestCarriesSubresourcesAndAspects) {
    constexpr VideoCore::ImageCopyRequest copy{
        .aspects = VideoCore::ImageCopyAspects::DepthStencil,
        .src = {1, 2},
        .dst = {0, 3},
        .src_layer_count = 2,
        .dst_layer_count = 2,
        .extent = {64, 32, 1},
    };
    static_assert(copy.Valid());
    EXPECT_EQ(copy.aspects, VideoCore::ImageCopyAspects::DepthStencil);
    EXPECT_EQ(copy.src.layer, 2u);
    EXPECT_EQ(copy.dst.layer, 3u);

    auto invalid = copy;
    invalid.src_layer_count = 0;
    EXPECT_FALSE(invalid.Valid());
    invalid = copy;
    invalid.extent.width = 0;
    EXPECT_FALSE(invalid.Valid());
}

TEST(ImageCommands, ClearRequestPreservesFloatingPointBits) {
    constexpr std::array<float, 4> values{0.0f, 0.5f, 1.0f, -1.0f};
    const VideoCore::ColorClearRequest clear{
        .component_bits = std::bit_cast<std::array<u32, 4>>(values),
        .range = {{2, 1}, {1, 3}},
    };
    const auto decoded = std::bit_cast<std::array<float, 4>>(clear.component_bits);
    EXPECT_EQ(decoded, values);
    EXPECT_EQ(clear.range.base.level, 2u);
    EXPECT_EQ(clear.range.extent.layers, 3u);
}

} // namespace
