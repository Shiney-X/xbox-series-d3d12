// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "video_core/texture_cache/image_buffer_copy.h"

namespace {

TEST(ImageBufferCopy, DescribesTransferWithoutNativeGraphicsTypes) {
    constexpr VideoCore::ImageBufferCopy copy{
        .buffer_offset = 4096,
        .buffer_row_length = 128,
        .buffer_image_height = 64,
        .mip_level = 2,
        .base_layer = 1,
        .layer_count = 3,
        .image_extent = {32, 16, 1},
    };
    static_assert(copy.Valid());
    EXPECT_EQ(copy.buffer_offset, 4096u);
    EXPECT_EQ(copy.mip_level, 2u);
    EXPECT_EQ(copy.layer_count, 3u);

    auto invalid = copy;
    invalid.layer_count = 0;
    EXPECT_FALSE(invalid.Valid());
    invalid = copy;
    invalid.image_extent.depth = 0;
    EXPECT_FALSE(invalid.Valid());
}

} // namespace
