// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "video_core/texture_cache/image_info.h"

namespace {

TEST(ImageResourceDesc, SeparatesGuestAllocationRequestFromVulkanFormat) {
    VideoCore::ImageInfo empty{};
    EXPECT_FALSE(empty.ResourceDesc().Valid());

    VideoCore::ImageInfo info{};
    info.guest_format = VideoCore::SurfaceImageFormat{AmdGpu::DataFormat::Format8_8_8_8,
                                                      AmdGpu::NumberFormat::Unorm};
    info.pixel_format = vk::Format::eR8G8B8A8Unorm;
    info.type = AmdGpu::ImageType::Color2D;
    info.size = {128, 64, 1};
    info.resources = {2, 3};
    info.num_samples = 1;

    const auto desc = info.ResourceDesc();
    ASSERT_TRUE(desc.Valid());
    EXPECT_EQ(desc.guest_format, info.guest_format);
    EXPECT_EQ(desc.type, info.type);
    EXPECT_EQ(desc.size, info.size);
    EXPECT_EQ(desc.resources, info.resources);
    EXPECT_EQ(desc.num_samples, info.num_samples);
    EXPECT_TRUE(VideoCore::HasUsage(desc.usage, VideoCore::ImageUsage::ColorAttachment));

    auto invalid = desc;
    invalid.resources.levels = 0;
    EXPECT_FALSE(invalid.Valid());
}

} // namespace
