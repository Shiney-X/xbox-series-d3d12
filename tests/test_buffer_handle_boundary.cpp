// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include "video_core/buffer_cache/buffer.h"
#include "video_core/texture_cache/image.h"
#include "video_core/texture_cache/tile_manager.h"

namespace {

TEST(BufferHandleBoundary, CacheOwnsOpaqueRendererResource) {
    static_assert(std::is_same_v<decltype(std::declval<const VideoCore::Buffer&>().Native()),
                                 const Vulkan::BufferResource&>);
    static_assert(!std::is_copy_constructible_v<VideoCore::Buffer>);
    SUCCEED();
}

TEST(BufferHandleBoundary, ImageTransfersUseResourceReferences) {
    using Resource = const Vulkan::BufferResource&;
    static_assert(std::is_same_v<decltype(&VideoCore::Image::Upload),
                                 void (VideoCore::Image::*)(
                                     std::span<const VideoCore::ImageBufferCopy>, Resource, u64)>);
    static_assert(
        std::is_same_v<decltype(&VideoCore::Image::Download),
                       void (VideoCore::Image::*)(std::span<const VideoCore::ImageBufferCopy>,
                                                  Resource, u64, u64)>);
    static_assert(std::is_same_v<decltype(&VideoCore::Image::CopyImageWithBuffer),
                                 void (VideoCore::Image::*)(VideoCore::Image&, Resource, u64)>);
    static_assert(std::is_same_v<decltype(&VideoCore::TileManager::DetileImage),
                                 VideoCore::TileManager::Result (VideoCore::TileManager::*)(
                                     Resource, u32, const VideoCore::ImageInfo&)>);
    SUCCEED();
}

} // namespace
