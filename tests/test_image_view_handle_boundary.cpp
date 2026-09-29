// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include "video_core/texture_cache/image_view.h"

namespace {

TEST(ImageViewHandleBoundary, CacheEntryExposesAdapterInsteadOfNativeHandle) {
    static_assert(std::is_same_v<decltype(std::declval<const VideoCore::ImageView&>().Native()),
                                 const Vulkan::ImageViewResource&>);
    static_assert(std::is_move_constructible_v<VideoCore::ImageView>);
    static_assert(!std::is_copy_constructible_v<VideoCore::ImageView>);
    SUCCEED();
}

} // namespace
