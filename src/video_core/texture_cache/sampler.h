// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "video_core/amdgpu/regs_texture.h"
#include "video_core/amdgpu/resource.h"

namespace Vulkan {
class Instance;
class SamplerResource;
} // namespace Vulkan

namespace VideoCore {

class Sampler {
public:
    explicit Sampler(const Vulkan::Instance& instance, const AmdGpu::Sampler& sampler,
                     const AmdGpu::BorderColorBuffer border_color_base);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    Sampler(Sampler&&) = default;
    Sampler& operator=(Sampler&&) = default;

    [[nodiscard]] std::shared_ptr<const Vulkan::SamplerResource> Native() const noexcept {
        return native;
    }

    size_t lru_id{};

private:
    std::shared_ptr<Vulkan::SamplerResource> native;
};

} // namespace VideoCore
