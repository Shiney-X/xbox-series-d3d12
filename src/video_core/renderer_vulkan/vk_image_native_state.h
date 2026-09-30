// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

// Vulkan-only properties selected while allocating a cached image.
struct ImageNativeState {
    vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor;
    vk::SampleCountFlags supported_samples = vk::SampleCountFlagBits::e1;
    vk::ImageUsageFlags usage_flags{};
    vk::FormatFeatureFlags2 format_features{};
};

} // namespace Vulkan
