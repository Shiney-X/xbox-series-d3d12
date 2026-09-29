// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/vk_common.h"

namespace VideoCore {
struct Image;
struct ImageViewInfo;
} // namespace VideoCore

namespace Vulkan {

class Instance;

// Owns the native image-view handle; cache entries keep this adapter opaque.
class ImageViewResource {
public:
    ImageViewResource(const Instance& instance, const VideoCore::ImageViewInfo& info,
                      const VideoCore::Image& image);

    [[nodiscard]] vk::ImageView Handle() const noexcept {
        return *handle;
    }

private:
    vk::UniqueImageView handle;
};

} // namespace Vulkan
