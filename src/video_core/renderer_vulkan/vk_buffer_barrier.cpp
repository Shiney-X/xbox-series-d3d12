// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_vulkan/vk_buffer_barrier.h"
#include "video_core/renderer_vulkan/vk_buffer_resource.h"

namespace Vulkan {

namespace {

struct VulkanBufferAccess {
    vk::AccessFlags2 access;
    vk::PipelineStageFlagBits2 stage;
};

constexpr VulkanBufferAccess ToVulkanAccess(VideoCore::BufferAccess access) {
    using VideoCore::BufferAccess;
    switch (access) {
    case BufferAccess::Initial:
        return {vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite |
                    vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eAllCommands};
    case BufferAccess::General:
        return {vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                vk::PipelineStageFlagBits2::eAllCommands};
    case BufferAccess::VertexRead:
        return {vk::AccessFlagBits2::eVertexAttributeRead,
                vk::PipelineStageFlagBits2::eVertexAttributeInput};
    case BufferAccess::IndexRead:
        return {vk::AccessFlagBits2::eIndexRead, vk::PipelineStageFlagBits2::eIndexInput};
    case BufferAccess::IndirectRead:
        return {vk::AccessFlagBits2::eIndirectCommandRead,
                vk::PipelineStageFlagBits2::eDrawIndirect};
    case BufferAccess::ShaderRead:
        return {vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eAllCommands};
    case BufferAccess::ShaderWrite:
        return {vk::AccessFlagBits2::eShaderWrite, vk::PipelineStageFlagBits2::eAllCommands};
    case BufferAccess::TransferRead:
        return {vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eTransfer};
    case BufferAccess::TransferWrite:
        return {vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eTransfer};
    }
    UNREACHABLE();
}

} // namespace

std::optional<vk::BufferMemoryBarrier2> GetBufferBarrier(VideoCore::Buffer& buffer,
                                                         VideoCore::BufferAccess next, u32 offset) {
    const auto transition = buffer.Transition(next, offset);
    if (!transition) {
        return std::nullopt;
    }
    return ToBufferBarrier(buffer.Native(), *transition);
}

vk::BufferMemoryBarrier2 ToBufferBarrier(const BufferResource& resource,
                                         const VideoCore::BufferTransition& transition) {
    const auto source = ToVulkanAccess(transition.before);
    const auto destination = ToVulkanAccess(transition.after);
    return vk::BufferMemoryBarrier2{
        .srcStageMask = source.stage,
        .srcAccessMask = source.access,
        .dstStageMask = destination.stage,
        .dstAccessMask = destination.access,
        .buffer = resource.Handle(),
        .offset = transition.offset,
        .size = transition.size,
    };
}

} // namespace Vulkan
