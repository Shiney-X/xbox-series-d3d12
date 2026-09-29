// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

VK_DEFINE_HANDLE(VmaAllocation)
VK_DEFINE_HANDLE(VmaAllocator)

namespace VideoCore {
struct BufferDesc;
}

namespace Vulkan {

class Instance;

// Owns a VMA-backed Vulkan buffer and its mapping/address metadata.
class BufferResource {
public:
    BufferResource(const Instance& instance, const VideoCore::BufferDesc& desc);
    ~BufferResource();

    BufferResource(const BufferResource&) = delete;
    BufferResource& operator=(const BufferResource&) = delete;
    BufferResource(BufferResource&&) = delete;
    BufferResource& operator=(BufferResource&&) = delete;

    [[nodiscard]] vk::Buffer Handle() const noexcept {
        return buffer;
    }

    [[nodiscard]] vk::DeviceAddress DeviceAddress() const noexcept {
        return bda_addr;
    }

    [[nodiscard]] std::span<u8> MappedData() const noexcept {
        return mapped_data;
    }

    [[nodiscard]] bool IsCoherent() const noexcept {
        return is_coherent;
    }

    void FlushMappedRange(u64 offset, u64 size) const;
    void InvalidateMappedRange(u64 offset, u64 size) const;

private:
    VmaAllocator allocator{};
    VmaAllocation allocation{};
    vk::Buffer buffer{};
    vk::DeviceAddress bda_addr{};
    std::span<u8> mapped_data{};
    bool is_coherent{};
};

} // namespace Vulkan
