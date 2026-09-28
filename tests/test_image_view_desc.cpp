// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "video_core/texture_cache/image_view.h"

namespace {

TEST(ImageViewDesc, PreservesGuestFormatButReusesEquivalentVulkanView) {
    VideoCore::ImageViewInfo srgb_view{};
    srgb_view.guest_format = VideoCore::SurfaceImageFormat{AmdGpu::DataFormat::Format8_8_8_8,
                                                           AmdGpu::NumberFormat::Srgb};
    srgb_view.format = vk::Format::eR8G8B8A8Unorm;

    auto unorm_view = srgb_view;
    unorm_view.guest_format = VideoCore::SurfaceImageFormat{AmdGpu::DataFormat::Format8_8_8_8,
                                                            AmdGpu::NumberFormat::Unorm};

    EXPECT_FALSE(static_cast<const VideoCore::ImageViewDesc&>(srgb_view) ==
                 static_cast<const VideoCore::ImageViewDesc&>(unorm_view));
    EXPECT_TRUE(srgb_view == unorm_view);

    unorm_view.format = vk::Format::eR8G8B8A8Srgb;
    EXPECT_FALSE(srgb_view == unorm_view);
}

} // namespace
