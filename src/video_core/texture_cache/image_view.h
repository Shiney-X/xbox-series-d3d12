// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "video_core/amdgpu/regs_depth.h"
#include "video_core/texture_cache/image_view_desc.h"

namespace AmdGpu {
struct ColorBuffer;
}

namespace Shader {
struct ImageResource;
}

namespace Vulkan {
class Instance;
class ImageViewResource;
class Scheduler;
} // namespace Vulkan

namespace VideoCore {

struct ImageViewInfo : ImageViewDesc {
    ImageViewInfo() = default;
    ImageViewInfo(const AmdGpu::Image& image, const Shader::ImageResource& desc) noexcept;
    ImageViewInfo(const AmdGpu::ColorBuffer& col_buffer) noexcept;
    ImageViewInfo(const AmdGpu::DepthBuffer& depth_buffer, AmdGpu::DepthView view,
                  AmdGpu::DepthControl ctl);

    bool operator==(const ImageViewInfo&) const = default;
};

struct Image;

struct ImageView {
    ImageView(const Vulkan::Instance& instance, const ImageViewInfo& info, const Image& image);
    ~ImageView();

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;

    ImageView(ImageView&&);
    ImageView& operator=(ImageView&&);

    [[nodiscard]] const Vulkan::ImageViewResource& Native() const noexcept;

    ImageViewInfo info;

private:
    std::unique_ptr<Vulkan::ImageViewResource> native;
};

} // namespace VideoCore
