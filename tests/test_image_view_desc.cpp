// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/image_view.h"

namespace {

template <typename T>
concept StoresNativeFormat = requires(const T& value) { value.pixel_format; };

template <typename T>
concept StoresNativeViewFormat = requires(const T& value) { value.format; };

TEST(ImageViewDesc, UsesGuestFormatInCacheIdentity) {
    static_assert(!StoresNativeFormat<VideoCore::ImageInfo>);
    static_assert(!StoresNativeViewFormat<VideoCore::ImageViewInfo>);
    VideoCore::ImageViewInfo srgb_view{};
    srgb_view.guest_format = VideoCore::SurfaceImageFormat{AmdGpu::DataFormat::Format8_8_8_8,
                                                           AmdGpu::NumberFormat::Srgb};

    auto unorm_view = srgb_view;
    unorm_view.guest_format = VideoCore::SurfaceImageFormat{AmdGpu::DataFormat::Format8_8_8_8,
                                                            AmdGpu::NumberFormat::Unorm};

    EXPECT_FALSE(static_cast<const VideoCore::ImageViewDesc&>(srgb_view) ==
                 static_cast<const VideoCore::ImageViewDesc&>(unorm_view));
    EXPECT_FALSE(srgb_view == unorm_view);

    unorm_view.guest_format = srgb_view.guest_format;
    EXPECT_TRUE(srgb_view == unorm_view);
}

} // namespace
