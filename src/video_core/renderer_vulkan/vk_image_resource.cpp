// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_image_resource.h"

#include <vk_mem_alloc.h>

namespace Vulkan {

ImageResource::~ImageResource() {
    Destroy();
}

ImageResource::ImageResource(ImageResource&& other) noexcept
    : allocator{std::exchange(other.allocator, VK_NULL_HANDLE)},
      allocation{std::exchange(other.allocation, VK_NULL_HANDLE)},
      image{std::exchange(other.image, vk::Image{})}, image_ci{std::move(other.image_ci)} {}

ImageResource& ImageResource::operator=(ImageResource&& other) noexcept {
    if (this != &other) {
        Destroy();
        allocator = std::exchange(other.allocator, VK_NULL_HANDLE);
        allocation = std::exchange(other.allocation, VK_NULL_HANDLE);
        image = std::exchange(other.image, vk::Image{});
        image_ci = std::move(other.image_ci);
    }
    return *this;
}

void ImageResource::Destroy() {
    if (image) {
        vmaDestroyImage(allocator, image, allocation);
        image = vk::Image{};
        allocation = VK_NULL_HANDLE;
    }
}

void ImageResource::Create(const vk::ImageCreateInfo& create_info) {
    image_ci = create_info;
    ASSERT(!image);
    const VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    const VkImageCreateInfo unsafe_create_info = static_cast<VkImageCreateInfo>(create_info);
    VkImage unsafe_image{};
    const VkResult result = vmaCreateImage(allocator, &unsafe_create_info, &alloc_info,
                                           &unsafe_image, &allocation, nullptr);
    ASSERT_MSG(result == VK_SUCCESS, "Failed allocating image with error {}",
               vk::to_string(vk::Result{result}));
    image = vk::Image{unsafe_image};
}

} // namespace Vulkan
