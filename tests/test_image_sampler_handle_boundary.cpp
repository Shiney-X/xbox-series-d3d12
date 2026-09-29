// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include "video_core/texture_cache/image.h"
#include "video_core/texture_cache/sampler.h"

namespace {

TEST(ImageSamplerHandleBoundary, CacheReturnsOpaqueAdapterResources) {
    static_assert(std::is_same_v<decltype(std::declval<const VideoCore::Image&>().Native()),
                                 const Vulkan::ImageResource&>);
    static_assert(std::is_same_v<decltype(std::declval<const VideoCore::Sampler&>().Native()),
                                 std::shared_ptr<const Vulkan::SamplerResource>>);
    static_assert(!std::is_copy_constructible_v<VideoCore::Image>);
    SUCCEED();
}

} // namespace
