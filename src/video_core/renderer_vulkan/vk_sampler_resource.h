// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/vk_common.h"

namespace AmdGpu {
struct Sampler;
struct BorderColorBuffer;
} // namespace AmdGpu

namespace Vulkan {

class Instance;

// Owns the native sampler handle; the cache keeps an opaque shared owner.
class SamplerResource {
public:
    SamplerResource(const Instance& instance, const AmdGpu::Sampler& sampler,
                    AmdGpu::BorderColorBuffer border_color_base);

    [[nodiscard]] vk::Sampler Handle() const noexcept {
        return *handle;
    }

private:
    vk::UniqueSampler handle;
};

} // namespace Vulkan
