// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include "video_core/buffer_cache/buffer.h"

namespace {

TEST(BufferHandleBoundary, CacheOwnsOpaqueRendererResource) {
    static_assert(std::is_same_v<decltype(std::declval<const VideoCore::Buffer&>().Native()),
                                 const Vulkan::BufferResource&>);
    static_assert(!std::is_copy_constructible_v<VideoCore::Buffer>);
    SUCCEED();
}

} // namespace
