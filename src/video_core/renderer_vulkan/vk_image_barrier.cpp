// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>

#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_image_barrier.h"
#include "video_core/renderer_vulkan/vk_image_native_state.h"
#include "video_core/renderer_vulkan/vk_image_resource.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan {

vk::ImageLayout ToVulkanLayout(VideoCore::ImageLayout layout) {
    using VideoCore::ImageLayout;
    switch (layout) {
    case ImageLayout::Undefined:
        return vk::ImageLayout::eUndefined;
    case ImageLayout::General:
        return vk::ImageLayout::eGeneral;
    case ImageLayout::TransferSource:
        return vk::ImageLayout::eTransferSrcOptimal;
    case ImageLayout::TransferDestination:
        return vk::ImageLayout::eTransferDstOptimal;
    case ImageLayout::ShaderReadOnly:
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    case ImageLayout::ColorAttachment:
        return vk::ImageLayout::eColorAttachmentOptimal;
    case ImageLayout::DepthAttachment:
        return vk::ImageLayout::eDepthAttachmentOptimal;
    case ImageLayout::DepthStencilAttachment:
        return vk::ImageLayout::eDepthStencilAttachmentOptimal;
    case ImageLayout::DepthReadOnly:
        return vk::ImageLayout::eDepthReadOnlyOptimal;
    case ImageLayout::DepthStencilReadOnly:
        return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    case ImageLayout::DepthReadOnlyStencilAttachment:
        return vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal;
    case ImageLayout::AttachmentFeedbackLoop:
        return vk::ImageLayout::eAttachmentFeedbackLoopOptimalEXT;
    }
    UNREACHABLE();
}

namespace {

constexpr std::array AccessMappings{
    std::pair{vk::AccessFlagBits2::eMemoryRead, VideoCore::ImageAccess::MemoryRead},
    std::pair{vk::AccessFlagBits2::eMemoryWrite, VideoCore::ImageAccess::MemoryWrite},
    std::pair{vk::AccessFlagBits2::eTransferRead, VideoCore::ImageAccess::TransferRead},
    std::pair{vk::AccessFlagBits2::eTransferWrite, VideoCore::ImageAccess::TransferWrite},
    std::pair{vk::AccessFlagBits2::eShaderRead, VideoCore::ImageAccess::ShaderRead},
    std::pair{vk::AccessFlagBits2::eShaderWrite, VideoCore::ImageAccess::ShaderWrite},
    std::pair{vk::AccessFlagBits2::eColorAttachmentRead,
              VideoCore::ImageAccess::ColorAttachmentRead},
    std::pair{vk::AccessFlagBits2::eColorAttachmentWrite,
              VideoCore::ImageAccess::ColorAttachmentWrite},
    std::pair{vk::AccessFlagBits2::eDepthStencilAttachmentRead,
              VideoCore::ImageAccess::DepthStencilRead},
    std::pair{vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
              VideoCore::ImageAccess::DepthStencilWrite},
};

vk::AccessFlags2 ToVulkanAccess(VideoCore::ImageAccess access) {
    vk::AccessFlags2 result{};
    for (const auto& [native, semantic] : AccessMappings) {
        if (VideoCore::HasImageAccess(access, semantic)) {
            result |= native;
        }
    }
    return result;
}

vk::PipelineStageFlags2 ToVulkanStage(VideoCore::ImageStage stage) {
    using VideoCore::ImageStage;
    switch (stage) {
    case ImageStage::AllCommands:
        return vk::PipelineStageFlagBits2::eAllCommands;
    case ImageStage::Transfer:
        return vk::PipelineStageFlagBits2::eTransfer;
    case ImageStage::GraphicsAndCompute:
        return vk::PipelineStageFlagBits2::eAllGraphics |
               vk::PipelineStageFlagBits2::eComputeShader;
    case ImageStage::FragmentShader:
        return vk::PipelineStageFlagBits2::eFragmentShader;
    case ImageStage::ColorAttachmentOutput:
        return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    case ImageStage::Copy:
        return vk::PipelineStageFlagBits2::eCopy;
    }
    UNREACHABLE();
}

} // namespace

ImageBarriers GetImageBarriers(VideoCore::Image& image, VideoCore::ImageResourceState next,
                               std::optional<VideoCore::SubresourceRange> range) {
    const auto transitions = image.GetTransitions(next, range);
    ImageBarriers barriers;
    for (const auto& transition : transitions) {
        barriers.emplace_back(vk::ImageMemoryBarrier2{
            .srcStageMask = ToVulkanStage(transition.before.stage),
            .srcAccessMask = ToVulkanAccess(transition.before.access),
            .dstStageMask = ToVulkanStage(transition.after.stage),
            .dstAccessMask = ToVulkanAccess(transition.after.access),
            .oldLayout = ToVulkanLayout(transition.before.layout),
            .newLayout = ToVulkanLayout(transition.after.layout),
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.Native().Handle(),
            .subresourceRange{
                .aspectMask = image.NativeState().aspect_mask,
                .baseMipLevel = transition.range.base.level,
                .levelCount = transition.whole_resource ? VK_REMAINING_MIP_LEVELS
                                                        : transition.range.extent.levels,
                .baseArrayLayer = transition.range.base.layer,
                .layerCount = transition.whole_resource ? VK_REMAINING_ARRAY_LAYERS
                                                        : transition.range.extent.layers,
            },
        });
    }
    return barriers;
}

void TransitImage(VideoCore::Image& image, VideoCore::ImageResourceState next,
                  std::optional<VideoCore::SubresourceRange> range, vk::CommandBuffer cmdbuf) {
    const auto barriers = GetImageBarriers(image, next, range);
    if (barriers.empty()) {
        return;
    }
    if (!cmdbuf) {
        image.scheduler->EndRendering();
        cmdbuf = image.scheduler->CommandBuffer();
    }
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
        .pImageMemoryBarriers = barriers.data(),
    });
}

} // namespace Vulkan
