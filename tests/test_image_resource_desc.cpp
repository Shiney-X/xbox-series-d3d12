// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include "video_core/texture_cache/image_info.h"

namespace {

TEST(ImageResourceDesc, ValidatesGuestAllocationRequestWithoutNativeFormat) {
    static_assert(
        std::is_same_v<decltype(std::declval<const VideoCore::ImageInfo&>().ResourceDesc()),
                       VideoCore::ImageResourceDesc>);

    VideoCore::ImageResourceDesc empty{};
    EXPECT_FALSE(empty.Valid());

    VideoCore::ImageResourceDesc desc{};
    desc.guest_format = VideoCore::SurfaceImageFormat{AmdGpu::DataFormat::Format8_8_8_8,
                                                      AmdGpu::NumberFormat::Unorm};
    desc.type = AmdGpu::ImageType::Color2D;
    desc.size = {128, 64, 1};
    desc.resources = {2, 3};
    desc.num_samples = 1;
    desc.usage = VideoCore::ImageUsage::ColorAttachment;

    ASSERT_TRUE(desc.Valid());
    EXPECT_TRUE(VideoCore::HasUsage(desc.usage, VideoCore::ImageUsage::ColorAttachment));

    auto invalid = desc;
    invalid.resources.levels = 0;
    EXPECT_FALSE(invalid.Valid());
}

} // namespace
