// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>

#include "common/assert.h"
#include "video_core/buffer_cache/buffer_desc.h"
#include "video_core/renderer_vulkan/vk_buffer_resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"

#include <vk_mem_alloc.h>

namespace Vulkan {

namespace {

vk::BufferUsageFlags ToVulkanUsage(VideoCore::BufferUsage usage) {
    using namespace VideoCore;
    vk::BufferUsageFlags flags{};
    if (HasUsage(usage, BufferUsage::TransferSource)) {
        flags |= vk::BufferUsageFlagBits::eTransferSrc;
    }
    if (HasUsage(usage, BufferUsage::TransferDestination)) {
        flags |= vk::BufferUsageFlagBits::eTransferDst;
    }
    if (HasUsage(usage, BufferUsage::Uniform)) {
        flags |= vk::BufferUsageFlagBits::eUniformBuffer;
    }
    if (HasUsage(usage, BufferUsage::Storage)) {
        flags |= vk::BufferUsageFlagBits::eStorageBuffer;
    }
    if (HasUsage(usage, BufferUsage::Index)) {
        flags |= vk::BufferUsageFlagBits::eIndexBuffer;
    }
    if (HasUsage(usage, BufferUsage::Vertex)) {
        flags |= vk::BufferUsageFlagBits::eVertexBuffer;
    }
    if (HasUsage(usage, BufferUsage::Indirect)) {
        flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
    }
    if (HasUsage(usage, BufferUsage::DeviceAddress)) {
        flags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
    }
    return flags;
}

VkMemoryPropertyFlags PreferredVmaFlags(VideoCore::MemoryUsage usage) {
    return usage != VideoCore::MemoryUsage::DeviceLocal ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                        : VkMemoryPropertyFlagBits{};
}

VmaAllocationCreateFlags VmaFlags(VideoCore::MemoryUsage usage) {
    switch (usage) {
    case VideoCore::MemoryUsage::Upload:
    case VideoCore::MemoryUsage::Stream:
        return VMA_ALLOCATION_CREATE_MAPPED_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    case VideoCore::MemoryUsage::Download:
        return VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    case VideoCore::MemoryUsage::DeviceLocal:
        return {};
    }
    return {};
}

VmaMemoryUsage VmaUsage(VideoCore::MemoryUsage usage) {
    switch (usage) {
    case VideoCore::MemoryUsage::DeviceLocal:
    case VideoCore::MemoryUsage::Stream:
        return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    case VideoCore::MemoryUsage::Upload:
    case VideoCore::MemoryUsage::Download:
        return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    }
    return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
}

} // namespace

BufferResource::BufferResource(const Instance& instance, const VideoCore::BufferDesc& desc,
                               bool within_budget)
    : allocator{instance.GetAllocator()} {
    const vk::BufferCreateInfo buffer_ci = {
        .size = desc.size_bytes,
        .usage = ToVulkanUsage(desc.usage),
    };
    const bool with_bda = bool(buffer_ci.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress);
    const VmaAllocationCreateFlags bda_flag =
        with_bda ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : 0;
    const VmaAllocationCreateInfo alloc_ci = {
        .flags = (within_budget ? VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT : 0) | bda_flag |
                 VmaFlags(desc.memory_usage),
        .usage = VmaUsage(desc.memory_usage),
        .requiredFlags = 0,
        .preferredFlags = PreferredVmaFlags(desc.memory_usage),
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    const VkBufferCreateInfo unsafe_ci = static_cast<VkBufferCreateInfo>(buffer_ci);
    VkBuffer unsafe_buffer{};
    VmaAllocationInfo alloc_info{};
    const VkResult result =
        vmaCreateBuffer(allocator, &unsafe_ci, &alloc_ci, &unsafe_buffer, &allocation, &alloc_info);
    ASSERT_MSG(result == VK_SUCCESS, "Failed allocating buffer with error {}",
               vk::to_string(vk::Result{result}));
    buffer = vk::Buffer{unsafe_buffer};

    if (with_bda) {
        const vk::BufferDeviceAddressInfo bda_info{.buffer = buffer};
        bda_addr = instance.GetDevice().getBufferAddress(bda_info);
        ASSERT_MSG(bda_addr != 0, "Failed to get buffer device address");
    }

    VkMemoryPropertyFlags property_flags{};
    vmaGetAllocationMemoryProperties(allocator, allocation, &property_flags);
    if (alloc_info.pMappedData) {
        mapped_data = std::span<u8>{std::bit_cast<u8*>(alloc_info.pMappedData), desc.size_bytes};
    }
    is_coherent = property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

BufferResource::~BufferResource() {
    if (buffer) {
        vmaDestroyBuffer(allocator, buffer, allocation);
    }
}

void BufferResource::FlushMappedRange(u64 offset, u64 size) const {
    vmaFlushAllocation(allocator, allocation, offset, size);
}

void BufferResource::InvalidateMappedRange(u64 offset, u64 size) const {
    vmaInvalidateAllocation(allocator, allocation, offset, size);
}

} // namespace Vulkan
