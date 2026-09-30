// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <boost/container/small_vector.hpp>

#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image_sync_state.h"

namespace VideoCore {
struct Image;
}

namespace Vulkan {

using ImageBarriers = boost::container::small_vector<vk::ImageMemoryBarrier2, 32>;

[[nodiscard]] vk::ImageLayout ToVulkanLayout(VideoCore::ImageLayout layout);
[[nodiscard]] ImageBarriers GetImageBarriers(VideoCore::Image& image,
                                             VideoCore::ImageResourceState next,
                                             std::optional<VideoCore::SubresourceRange> range);
void TransitImage(VideoCore::Image& image, VideoCore::ImageResourceState next,
                  std::optional<VideoCore::SubresourceRange> range, vk::CommandBuffer cmdbuf = {});

} // namespace Vulkan
