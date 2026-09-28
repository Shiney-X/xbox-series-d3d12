// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "video_core/texture_cache/image_usage.h"

namespace {

using VideoCore::HasUsage;
using VideoCore::ImageUsage;

TEST(ImageUsage, DescribesColorDepthAndCompressedImagesWithoutNativeFlags) {
    const auto color = VideoCore::CachedImageUsage(false, false);
    EXPECT_TRUE(HasUsage(color, ImageUsage::TransferSource));
    EXPECT_TRUE(HasUsage(color, ImageUsage::TransferDestination));
    EXPECT_TRUE(HasUsage(color, ImageUsage::Sampled));
    EXPECT_TRUE(HasUsage(color, ImageUsage::ColorAttachment));
    EXPECT_TRUE(HasUsage(color, ImageUsage::Storage));
    EXPECT_FALSE(HasUsage(color, ImageUsage::DepthStencilAttachment));

    const auto depth = VideoCore::CachedImageUsage(false, true);
    EXPECT_TRUE(HasUsage(depth, ImageUsage::DepthStencilAttachment));
    EXPECT_FALSE(HasUsage(depth, ImageUsage::ColorAttachment));
    EXPECT_FALSE(HasUsage(depth, ImageUsage::Storage));

    const auto compressed = VideoCore::CachedImageUsage(true, false);
    EXPECT_TRUE(HasUsage(compressed, ImageUsage::Storage));
    EXPECT_FALSE(HasUsage(compressed, ImageUsage::ColorAttachment));
    EXPECT_FALSE(HasUsage(compressed, ImageUsage::DepthStencilAttachment));
}

} // namespace
