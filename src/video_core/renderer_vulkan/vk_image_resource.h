// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <utility>

#include "video_core/renderer_vulkan/vk_common.h"

VK_DEFINE_HANDLE(VmaAllocation)
VK_DEFINE_HANDLE(VmaAllocator)

namespace Vulkan {

// Owns a VMA-backed Vulkan image. Caches may retain this adapter opaquely.
class ImageResource {
public:
    ImageResource() = default;
    explicit ImageResource(VmaAllocator allocator) : allocator{allocator} {}
    ~ImageResource();

    ImageResource(const ImageResource&) = delete;
    ImageResource& operator=(const ImageResource&) = delete;
    ImageResource(ImageResource&& other) noexcept;
    ImageResource& operator=(ImageResource&& other) noexcept;

    void Create(const vk::ImageCreateInfo& create_info);
    void Destroy();

    [[nodiscard]] vk::Image Handle() const noexcept {
        return image;
    }

    [[nodiscard]] const vk::ImageCreateInfo& CreateInfo() const noexcept {
        return image_ci;
    }

    operator vk::Image() const noexcept {
        return image;
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(image);
    }

private:
    VmaAllocator allocator{};
    VmaAllocation allocation{};
    vk::Image image{};
    vk::ImageCreateInfo image_ci{};
};

} // namespace Vulkan
