// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <ranges>
#include <utility>
#include "common/assert.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_blit_helper.h"
#include "video_core/renderer_vulkan/vk_buffer_resource.h"
#include "video_core/renderer_vulkan/vk_image_resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"

namespace VideoCore {

using namespace Vulkan;

Common::IncrementalIdProvider<u64> Image::global_image_uid{};

namespace {

vk::ImageAspectFlags ToVulkanAspects(ImageCopyAspects aspects) {
    switch (aspects) {
    case ImageCopyAspects::Color:
        return vk::ImageAspectFlagBits::eColor;
    case ImageCopyAspects::Depth:
        return vk::ImageAspectFlagBits::eDepth;
    case ImageCopyAspects::Stencil:
        return vk::ImageAspectFlagBits::eStencil;
    case ImageCopyAspects::DepthStencil:
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    }
    UNREACHABLE();
}

vk::BufferImageCopy ToVulkanCopy(const ImageBufferCopy& copy, vk::ImageAspectFlags aspect) {
    ASSERT(copy.Valid());
    return {
        .bufferOffset = copy.buffer_offset,
        .bufferRowLength = copy.buffer_row_length,
        .bufferImageHeight = copy.buffer_image_height,
        .imageSubresource{
            .aspectMask = aspect,
            .mipLevel = copy.mip_level,
            .baseArrayLayer = copy.base_layer,
            .layerCount = copy.layer_count,
        },
        .imageOffset = {copy.image_offset.x, copy.image_offset.y, copy.image_offset.z},
        .imageExtent = {copy.image_extent.width, copy.image_extent.height, copy.image_extent.depth},
    };
}

vk::ImageLayout ToVulkanLayout(ImageLayout layout) {
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

constexpr std::array AccessMappings{
    std::pair{vk::AccessFlagBits2::eMemoryRead, ImageAccess::MemoryRead},
    std::pair{vk::AccessFlagBits2::eMemoryWrite, ImageAccess::MemoryWrite},
    std::pair{vk::AccessFlagBits2::eTransferRead, ImageAccess::TransferRead},
    std::pair{vk::AccessFlagBits2::eTransferWrite, ImageAccess::TransferWrite},
    std::pair{vk::AccessFlagBits2::eShaderRead, ImageAccess::ShaderRead},
    std::pair{vk::AccessFlagBits2::eShaderWrite, ImageAccess::ShaderWrite},
    std::pair{vk::AccessFlagBits2::eColorAttachmentRead, ImageAccess::ColorAttachmentRead},
    std::pair{vk::AccessFlagBits2::eColorAttachmentWrite, ImageAccess::ColorAttachmentWrite},
    std::pair{vk::AccessFlagBits2::eDepthStencilAttachmentRead, ImageAccess::DepthStencilRead},
    std::pair{vk::AccessFlagBits2::eDepthStencilAttachmentWrite, ImageAccess::DepthStencilWrite},
};

vk::AccessFlags2 ToVulkanAccess(ImageAccess access) {
    vk::AccessFlags2 result{};
    for (const auto& [native, semantic] : AccessMappings) {
        if (HasImageAccess(access, semantic)) {
            result |= native;
        }
    }
    return result;
}

vk::PipelineStageFlags2 ToVulkanStage(ImageStage stage) {
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

static vk::ImageUsageFlags ToVulkanUsage(const Vulkan::Instance& instance, ImageUsage requested) {
    vk::ImageUsageFlags usage{};
    if (HasUsage(requested, ImageUsage::TransferSource)) {
        usage |= vk::ImageUsageFlagBits::eTransferSrc;
    }
    if (HasUsage(requested, ImageUsage::TransferDestination)) {
        usage |= vk::ImageUsageFlagBits::eTransferDst;
    }
    if (HasUsage(requested, ImageUsage::Sampled)) {
        usage |= vk::ImageUsageFlagBits::eSampled;
    }
    if (HasUsage(requested, ImageUsage::ColorAttachment)) {
        usage |= vk::ImageUsageFlagBits::eColorAttachment;
        if (instance.IsAttachmentFeedbackLoopLayoutSupported()) {
            usage |= vk::ImageUsageFlagBits::eAttachmentFeedbackLoopEXT;
        }
    }
    if (HasUsage(requested, ImageUsage::DepthStencilAttachment)) {
        usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    }
    if (HasUsage(requested, ImageUsage::Storage)) {
        usage |= vk::ImageUsageFlagBits::eStorage;
    }
    return usage;
}

static vk::ImageType ConvertImageType(AmdGpu::ImageType type) noexcept {
    switch (type) {
    case AmdGpu::ImageType::Color1D:
    case AmdGpu::ImageType::Color1DArray:
        return vk::ImageType::e1D;
    case AmdGpu::ImageType::Color2D:
    case AmdGpu::ImageType::Color2DMsaa:
    case AmdGpu::ImageType::Color2DArray:
        return vk::ImageType::e2D;
    case AmdGpu::ImageType::Color3D:
        return vk::ImageType::e3D;
    default:
        UNREACHABLE();
    }
}

static vk::FormatFeatureFlags2 FormatFeatureFlags(const vk::ImageUsageFlags usage_flags) {
    vk::FormatFeatureFlags2 feature_flags{};
    if (usage_flags & vk::ImageUsageFlagBits::eTransferSrc) {
        feature_flags |= vk::FormatFeatureFlagBits2::eTransferSrc;
    }
    if (usage_flags & vk::ImageUsageFlagBits::eTransferDst) {
        feature_flags |= vk::FormatFeatureFlagBits2::eTransferDst;
    }
    if (usage_flags & vk::ImageUsageFlagBits::eSampled) {
        feature_flags |= vk::FormatFeatureFlagBits2::eSampledImage;
    }
    if (usage_flags & vk::ImageUsageFlagBits::eColorAttachment) {
        feature_flags |= vk::FormatFeatureFlagBits2::eColorAttachment;
    }
    if (usage_flags & vk::ImageUsageFlagBits::eDepthStencilAttachment) {
        feature_flags |= vk::FormatFeatureFlagBits2::eDepthStencilAttachment;
    }
    // Note: StorageImage is intentionally ignored for now since it is always set, and can mess up
    // compatibility checks.
    return feature_flags;
}

Image::Image(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_,
             Vulkan::BlitHelper& blit_helper_, Common::SlotVector<ImageView>& slot_image_views_,
             const ImageInfo& info_, ImageResourceDesc resource_desc_)
    : instance{&instance_}, scheduler{&scheduler_}, blit_helper{&blit_helper_},
      slot_image_views{&slot_image_views_}, info{info_}, resource_desc{std::move(resource_desc_)} {
    if (!resource_desc.Valid()) {
        return;
    }
    const auto requested_format = LiverpoolToVK::ImageFormat(resource_desc.guest_format);
    if (requested_format == vk::Format::eUndefined) {
        return;
    }
    ASSERT_MSG(requested_format == info.pixel_format,
               "Guest image format changed before allocation");
    image_uid = global_image_uid.Next();
    mip_hashes.resize(resource_desc.resources.levels);
    // Here we force `eExtendedUsage` as don't know all image usage cases beforehand. In normal case
    // the texture cache should re-create the resource with the usage requested
    vk::ImageCreateFlags flags{vk::ImageCreateFlagBits::eMutableFormat |
                               vk::ImageCreateFlagBits::eExtendedUsage};
    if (resource_desc.is_volume) {
        flags |= vk::ImageCreateFlagBits::e2DArrayCompatible;
        if (instance->Is2dViewOf3dSupported()) {
            flags |= vk::ImageCreateFlagBits::e2DViewCompatibleEXT;
        }
    }
    if (resource_desc.is_block && instance->IsBlockTexelViewSupported()) {
        flags |= vk::ImageCreateFlagBits::eBlockTexelViewCompatible;
    }

    usage_flags = ToVulkanUsage(*instance, resource_desc.usage);
    format_features = FormatFeatureFlags(usage_flags);
    if (resource_desc.is_depth) {
        aspect_mask = vk::ImageAspectFlagBits::eDepth;
        if (resource_desc.has_stencil) {
            aspect_mask |= vk::ImageAspectFlagBits::eStencil;
        }
    }

    constexpr auto tiling = vk::ImageTiling::eOptimal;
    const auto supported_format = instance->GetSupportedFormat(requested_format, format_features);
    const vk::PhysicalDeviceImageFormatInfo2 format_info{
        .format = supported_format,
        .type = ConvertImageType(resource_desc.type),
        .tiling = tiling,
        .usage = usage_flags,
        .flags = flags,
    };
    const auto image_format_properties =
        instance->GetPhysicalDevice().getImageFormatProperties2(format_info);
    if (image_format_properties.result == vk::Result::eErrorFormatNotSupported) {
        LOG_ERROR(Render_Vulkan, "image format {} type {} is not supported (flags {}, usage {})",
                  vk::to_string(supported_format), vk::to_string(format_info.type),
                  vk::to_string(format_info.flags), vk::to_string(format_info.usage));
    }
    supported_samples = image_format_properties.result == vk::Result::eSuccess
                            ? image_format_properties.value.imageFormatProperties.sampleCounts
                            : vk::SampleCountFlagBits::e1;

    const vk::ImageCreateInfo image_ci = {
        .flags = flags,
        .imageType = ConvertImageType(resource_desc.type),
        .format = supported_format,
        .extent{
            .width = resource_desc.size.width,
            .height = resource_desc.size.height,
            .depth = resource_desc.size.depth,
        },
        .mipLevels = resource_desc.resources.levels,
        .arrayLayers = resource_desc.resources.layers,
        .samples = LiverpoolToVK::NumSamples(resource_desc.num_samples, supported_samples),
        .tiling = tiling,
        .usage = usage_flags,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    backing = &backing_images.emplace_back();
    backing->sync_state = ImageSyncState{resource_desc.resources};
    backing->num_samples = resource_desc.num_samples;
    backing->image = std::make_unique<Vulkan::ImageResource>(instance->GetAllocator());
    backing->image->Create(image_ci);

    Vulkan::SetObjectName(instance->GetDevice(), GetImage(),
                          "Image {}x{}x{} {} {} {:#x}:{:#x} L:{} M:{} S:{}", info.size.width,
                          info.size.height, info.size.depth, AmdGpu::NameOf(info.tile_mode),
                          vk::to_string(info.pixel_format), info.guest_address, info.guest_size,
                          info.resources.layers, info.resources.levels, info.num_samples);
}

Image::~Image() = default;

const Vulkan::ImageResource& Image::Native() const noexcept {
    return *backing->image;
}

vk::Image Image::GetImage() const {
    return Native().Handle();
}

ImageView& Image::FindView(const ImageViewInfo& view_info, bool ensure_guest_samples) {
    if (ensure_guest_samples && backing->num_samples > 1 != info.num_samples > 1) {
        SetBackingSamples(info.num_samples);
    }
    const auto& view_infos = backing->image_view_infos;
    const auto it = std::ranges::find(view_infos, view_info);
    if (it != view_infos.end()) {
        const auto view_id = backing->image_view_ids[std::distance(view_infos.begin(), it)];
        return (*slot_image_views)[view_id];
    }
    const auto view_id = slot_image_views->insert(*instance, view_info, *this);
    backing->image_view_infos.emplace_back(view_info);
    backing->image_view_ids.emplace_back(view_id);
    return (*slot_image_views)[view_id];
}

vk::ImageLayout Image::CurrentLayout() const {
    return ToVulkanLayout(backing->sync_state.Current().layout);
}

Image::Barriers Image::GetBarriers(ImageResourceState next,
                                   std::optional<SubresourceRange> subres_range) {
    const auto transitions = backing->sync_state.Transition(next, subres_range);
    Barriers barriers;
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
            .image = GetImage(),
            .subresourceRange{
                .aspectMask = aspect_mask,
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

void Image::Transit(ImageResourceState next, std::optional<SubresourceRange> range,
                    vk::CommandBuffer cmdbuf /*= {}*/) {
    const auto barriers = GetBarriers(next, range);
    if (barriers.empty()) {
        return;
    }

    if (!cmdbuf) {
        // When using external cmdbuf you are responsible for ending rp.
        scheduler->EndRendering();
        cmdbuf = scheduler->CommandBuffer();
    }
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
        .pImageMemoryBarriers = barriers.data(),
    });
}

void Image::Upload(std::span<const ImageBufferCopy> upload_copies,
                   const Vulkan::BufferResource& buffer, u64 offset) {
    const vk::Buffer native_buffer = buffer.Handle();
    SetBackingSamples(info.num_samples, false);
    scheduler->EndRendering();

    const vk::BufferMemoryBarrier2 pre_barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .buffer = native_buffer,
        .offset = offset,
        .size = info.guest_size,
    };
    const vk::BufferMemoryBarrier2 post_barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .buffer = native_buffer,
        .offset = offset,
        .size = info.guest_size,
    };
    const auto image_barriers = GetBarriers(
        {ImageLayout::TransferDestination, ImageAccess::TransferWrite, ImageStage::Copy}, {});
    const auto cmdbuf = scheduler->CommandBuffer();
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &pre_barrier,
        .imageMemoryBarrierCount = static_cast<u32>(image_barriers.size()),
        .pImageMemoryBarriers = image_barriers.data(),
    });
    boost::container::small_vector<vk::BufferImageCopy, 14> native_copies;
    for (const auto& copy : upload_copies) {
        native_copies.push_back(
            ToVulkanCopy(copy, aspect_mask & ~vk::ImageAspectFlagBits::eStencil));
    }
    cmdbuf.copyBufferToImage(native_buffer, GetImage(), vk::ImageLayout::eTransferDstOptimal,
                             native_copies);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &post_barrier,
    });
    Transit(ImageStates::GeneralShaderTransferRead, {});
    flags &= ~ImageFlagBits::Dirty;
}

void Image::Download(std::span<const ImageBufferCopy> download_copies,
                     const Vulkan::BufferResource& buffer, u64 offset, u64 download_size) {
    const vk::Buffer native_buffer = buffer.Handle();
    SetBackingSamples(info.num_samples);
    scheduler->EndRendering();

    const vk::BufferMemoryBarrier2 pre_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .buffer = native_buffer,
        .offset = offset,
        .size = download_size,
    };
    const vk::BufferMemoryBarrier2 post_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead,
        .buffer = native_buffer,
        .offset = offset,
        .size = download_size,
    };
    const auto image_barriers =
        GetBarriers({ImageLayout::TransferSource, ImageAccess::TransferRead, ImageStage::Copy}, {});
    auto cmdbuf = scheduler->CommandBuffer();
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &pre_barrier,
        .imageMemoryBarrierCount = static_cast<u32>(image_barriers.size()),
        .pImageMemoryBarriers = image_barriers.data(),
    });
    boost::container::small_vector<vk::BufferImageCopy, 8> native_copies;
    for (const auto& copy : download_copies) {
        native_copies.push_back(
            ToVulkanCopy(copy, aspect_mask & ~vk::ImageAspectFlagBits::eStencil));
    }
    cmdbuf.copyImageToBuffer(GetImage(), vk::ImageLayout::eTransferSrcOptimal, native_buffer,
                             native_copies);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &post_barrier,
    });
}

static std::pair<u32, u32> SanitizeCopyLayers(const ImageInfo& src_info, const ImageInfo& dst_info,
                                              const u32 depth) {
    const auto vk_src_type = ConvertImageType(src_info.type);
    const auto vk_dst_type = ConvertImageType(dst_info.type);

    u32 src_layers = src_info.resources.layers;
    u32 dst_layers = dst_info.resources.layers;

    // 3D images can only use 1 layer.
    if (vk_src_type == vk::ImageType::e3D && src_layers != 1) {
        LOG_WARNING(Render_Vulkan, "Coercing copy 3D source layers {} to 1.", src_layers);
        src_layers = 1;
    }
    if (vk_dst_type == vk::ImageType::e3D && dst_layers != 1) {
        LOG_WARNING(Render_Vulkan, "Coercing copy 3D destination layers {} to 1.", dst_layers);
        dst_layers = 1;
    }

    // If the image type is equal, layer count must match. Take the minimum of both.
    if (vk_src_type == vk_dst_type) {
        if (src_layers != dst_layers) {
            LOG_WARNING(Render_Vulkan,
                        "Coercing copy source layers {} and destination layers {} to minimum.",
                        src_layers, dst_layers);
            src_layers = dst_layers = std::min(src_layers, dst_layers);
        }
    } else {
        // For 2D <-> 3D copies, 2D layer count must equal 3D depth.
        if (vk_src_type == vk::ImageType::e2D && vk_dst_type == vk::ImageType::e3D &&
            src_layers != depth) {
            LOG_WARNING(Render_Vulkan,
                        "Coercing copy 2D source layers {} to 3D destination depth {}", src_layers,
                        depth);
            src_layers = depth;
        }
        if (vk_src_type == vk::ImageType::e3D && vk_dst_type == vk::ImageType::e2D &&
            dst_layers != depth) {
            LOG_WARNING(Render_Vulkan,
                        "Coercing copy 2D destination layers {} to 3D source depth {}", dst_layers,
                        depth);
            dst_layers = depth;
        }
    }

    return std::make_pair(src_layers, dst_layers);
}

void Image::CopyImage(Image& src_image) {
    const auto& src_info = src_image.info;

    const u32 num_mips = std::min(src_info.resources.levels, info.resources.levels);

    // Format mismatch warning (safe but useful)
    if (src_info.pixel_format != info.pixel_format) {
        LOG_DEBUG(Render_Vulkan,
                  "Copy between different formats: src={}, dst={}. "
                  "Result may be undefined.",
                  vk::to_string(src_info.pixel_format), vk::to_string(info.pixel_format));
    }

    const u32 base_width = src_info.size.width;
    const u32 base_height = src_info.size.height;
    const u32 base_depth =
        info.type == AmdGpu::ImageType::Color3D ? info.size.depth : src_info.size.depth;

    // Match sample count before copying
    SetBackingSamples(info.num_samples, false);
    src_image.SetBackingSamples(src_info.num_samples);

    boost::container::small_vector<vk::ImageCopy, 8> regions;

    const vk::ImageAspectFlags src_aspect =
        src_image.aspect_mask & ~vk::ImageAspectFlagBits::eStencil;

    const vk::ImageAspectFlags dst_aspect = aspect_mask & ~vk::ImageAspectFlagBits::eStencil;

    const bool src_is_2d = ConvertImageType(src_info.type) == vk::ImageType::e2D;
    const bool src_is_3d = ConvertImageType(src_info.type) == vk::ImageType::e3D;

    const bool dst_is_2d = ConvertImageType(info.type) == vk::ImageType::e2D;
    const bool dst_is_3d = ConvertImageType(info.type) == vk::ImageType::e3D;

    const bool is_2d_to_3d = src_is_2d && dst_is_3d;
    const bool is_3d_to_2d = src_is_3d && dst_is_2d;
    const bool is_same_type = !is_2d_to_3d && !is_3d_to_2d;

    for (u32 mip = 0; mip < num_mips; ++mip) {
        const u32 mip_w = std::max(base_width >> mip, 1u);
        const u32 mip_h = std::max(base_height >> mip, 1u);
        const u32 mip_d = std::max(base_depth >> mip, 1u);

        auto [src_layers, dst_layers] = SanitizeCopyLayers(src_info, info, mip_d);

        vk::ImageCopy region{};

        region.srcSubresource.aspectMask = src_aspect;
        region.srcSubresource.mipLevel = mip;
        region.srcSubresource.baseArrayLayer = 0;

        region.dstSubresource.aspectMask = dst_aspect;
        region.dstSubresource.mipLevel = mip;
        region.dstSubresource.baseArrayLayer = 0;

        if (is_same_type) {
            // 2D->2D OR 3D->3D
            if (src_is_3d) {
                // 3D images must use layerCount=1
                region.srcSubresource.layerCount = 1;
                region.dstSubresource.layerCount = 1;
                region.extent = vk::Extent3D(mip_w, mip_h, mip_d);
            } else {
                // Array images
                const u32 copy_layers = std::min(src_layers, dst_layers);
                region.srcSubresource.layerCount = copy_layers;
                region.dstSubresource.layerCount = copy_layers;
                region.extent = vk::Extent3D(mip_w, mip_h, 1);
            }
        } else if (is_2d_to_3d) {
            // 2D array -> 3D volume
            region.srcSubresource.layerCount = src_layers;
            region.dstSubresource.layerCount = 1;
            region.extent = vk::Extent3D(mip_w, mip_h, src_layers);
        } else if (is_3d_to_2d) {
            // 3D volume -> 2D array
            region.srcSubresource.layerCount = 1;
            region.dstSubresource.layerCount = dst_layers;
            region.extent = vk::Extent3D(mip_w, mip_h, dst_layers);
        }

        regions.push_back(region);
    }

    scheduler->EndRendering();

    src_image.Transit(ImageStates::TransferSource, {});

    Transit(ImageStates::TransferDestination, {});

    auto cmdbuf = scheduler->CommandBuffer();

    if (!regions.empty()) {
        cmdbuf.copyImage(src_image.GetImage(), src_image.CurrentLayout(), GetImage(),
                         CurrentLayout(), regions);
    }

    Transit(ImageStates::GeneralShaderTransferRead, {});
}

void Image::CopyRegion(Image& src_image, const ImageCopyRequest& request) {
    ASSERT(request.Valid());
    const SubresourceRange src_range{request.src, {1, request.src_layer_count}};
    const SubresourceRange dst_range{request.dst, {1, request.dst_layer_count}};

    scheduler->EndRendering();
    src_image.Transit(ImageStates::TransferSource, src_range);
    Transit(ImageStates::TransferDestination, dst_range);

    const auto aspect = ToVulkanAspects(request.aspects);
    const vk::ImageCopy region{
        .srcSubresource{
            .aspectMask = aspect,
            .mipLevel = request.src.level,
            .baseArrayLayer = request.src.layer,
            .layerCount = request.src_layer_count,
        },
        .srcOffset = {request.src_offset.x, request.src_offset.y, request.src_offset.z},
        .dstSubresource{
            .aspectMask = aspect,
            .mipLevel = request.dst.level,
            .baseArrayLayer = request.dst.layer,
            .layerCount = request.dst_layer_count,
        },
        .dstOffset = {request.dst_offset.x, request.dst_offset.y, request.dst_offset.z},
        .extent = {request.extent.width, request.extent.height, request.extent.depth},
    };
    scheduler->CommandBuffer().copyImage(src_image.GetImage(), src_image.CurrentLayout(),
                                         GetImage(), CurrentLayout(), region);
}

void Image::CopyImageWithBuffer(Image& src_image, const Vulkan::BufferResource& buffer,
                                u64 offset) {
    const vk::Buffer native_buffer = buffer.Handle();
    const auto& src_info = src_image.info;
    const u32 num_mips = std::min(src_info.resources.levels, info.resources.levels);
    const u32 num_layers = std::min(src_info.resources.layers, info.resources.layers);
    ASSERT(src_info.resources.layers == info.resources.layers || num_mips == 1);

    SetBackingSamples(info.num_samples, false);
    src_image.SetBackingSamples(src_info.num_samples);

    boost::container::small_vector<vk::BufferImageCopy, 8> buffer_copies;
    for (u32 mip = 0; mip < num_mips; ++mip) {
        const auto mip_w = std::max(src_info.size.width >> mip, 1u);
        const auto mip_h = std::max(src_info.size.height >> mip, 1u);
        const auto mip_d = std::max(src_info.size.depth >> mip, 1u);

        buffer_copies.emplace_back(vk::BufferImageCopy{
            .bufferOffset = offset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource{
                .aspectMask = src_image.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = num_layers,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {mip_w, mip_h, mip_d},
        });
    }

    const vk::BufferMemoryBarrier2 pre_copy_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .buffer = native_buffer,
        .offset = offset,
        .size = VK_WHOLE_SIZE,
    };

    const vk::BufferMemoryBarrier2 post_copy_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .buffer = native_buffer,
        .offset = offset,
        .size = VK_WHOLE_SIZE,
    };

    scheduler->EndRendering();
    src_image.Transit(ImageStates::TransferSource, {});
    Transit(ImageStates::TransferDestination, {});

    auto cmdbuf = scheduler->CommandBuffer();
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &pre_copy_barrier,
    });

    cmdbuf.copyImageToBuffer(src_image.GetImage(), vk::ImageLayout::eTransferSrcOptimal,
                             native_buffer, buffer_copies);

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &post_copy_barrier,
    });

    for (auto& copy : buffer_copies) {
        copy.imageSubresource.aspectMask = aspect_mask & ~vk::ImageAspectFlagBits::eStencil;
    }

    cmdbuf.copyBufferToImage(native_buffer, GetImage(), vk::ImageLayout::eTransferDstOptimal,
                             buffer_copies);
    Transit(ImageStates::GeneralShaderTransferRead, {});
}

void Image::CopyMip(Image& src_image, u32 mip, u32 slice) {
    const auto& src_info = src_image.info;

    const auto dst_dim = info.props.is_block ? 2 : 0;
    const auto mip_block_w = std::max(info.size.width >> (mip + dst_dim), 1u);
    const auto mip_block_h = std::max(info.size.height >> (mip + dst_dim), 1u);
    const auto mip_block_p = std::max(info.mips_layout[mip].pitch >> dst_dim, 1u);

    const auto src_dim = src_info.props.is_block ? 2 : 0;
    ASSERT(mip_block_w == (src_info.size.width >> src_dim));
    ASSERT(mip_block_h == (src_info.size.height >> src_dim));
    ASSERT(mip_block_p == (src_info.pitch >> src_dim));

    const auto [src_layers, dst_layers] = SanitizeCopyLayers(src_info, info, src_info.size.depth);

    const vk::ImageCopy image_copy{
        .srcSubresource{
            .aspectMask = src_image.aspect_mask,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = src_layers,
        },
        .dstSubresource{
            .aspectMask = src_image.aspect_mask,
            .mipLevel = mip,
            .baseArrayLayer = slice,
            .layerCount = dst_layers,
        },
        .extent = {src_info.size.width, src_info.size.height, src_info.size.depth},
    };

    SetBackingSamples(info.num_samples);
    src_image.SetBackingSamples(src_info.num_samples);

    scheduler->EndRendering();
    Transit(ImageStates::TransferDestination, {});
    src_image.Transit(ImageStates::TransferSource, {});

    const auto cmdbuf = scheduler->CommandBuffer();
    cmdbuf.copyImage(src_image.GetImage(), src_image.CurrentLayout(), GetImage(), CurrentLayout(),
                     image_copy);
    Transit(ImageStates::GeneralShaderTransferRead, {});
}

void Image::Resolve(Image& src_image, const VideoCore::SubresourceRange& mrt0_range,
                    const VideoCore::SubresourceRange& mrt1_range) {
    SetBackingSamples(1, false);
    scheduler->EndRendering();

    src_image.Transit(ImageStates::TransferSource, mrt0_range);
    Transit(ImageStates::TransferDestination, mrt1_range);

    const auto [src_layers, dst_layers] = SanitizeCopyLayers(src_image.info, info, 1);
    if (src_image.backing->num_samples == 1) {
        const vk::ImageCopy region = {
            .srcSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = mrt0_range.base.layer,
                .layerCount = src_layers,
            },
            .srcOffset = {0, 0, 0},
            .dstSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = mrt1_range.base.layer,
                .layerCount = dst_layers,
            },
            .dstOffset = {0, 0, 0},
            .extent = {info.size.width, info.size.height, 1},
        };
        scheduler->CommandBuffer().copyImage(src_image.GetImage(),
                                             vk::ImageLayout::eTransferSrcOptimal, GetImage(),
                                             vk::ImageLayout::eTransferDstOptimal, region);
    } else {
        const vk::ImageResolve region = {
            .srcSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = mrt0_range.base.layer,
                .layerCount = src_layers,
            },
            .srcOffset = {0, 0, 0},
            .dstSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = mrt1_range.base.layer,
                .layerCount = dst_layers,
            },
            .dstOffset = {0, 0, 0},
            .extent = {info.size.width, info.size.height, 1},
        };
        scheduler->CommandBuffer().resolveImage(src_image.GetImage(),
                                                vk::ImageLayout::eTransferSrcOptimal, GetImage(),
                                                vk::ImageLayout::eTransferDstOptimal, region);
    }

    flags |= VideoCore::ImageFlagBits::GpuModified;
    flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Image::Clear(const ColorClearRequest& request) {
    const vk::ImageSubresourceRange vk_range = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = request.range.base.level,
        .levelCount = request.range.extent.levels,
        .baseArrayLayer = request.range.base.layer,
        .layerCount = request.range.extent.layers,
    };
    scheduler->EndRendering();
    Transit(ImageStates::TransferDestination, {});
    const auto cmdbuf = scheduler->CommandBuffer();
    const vk::ClearColorValue color{.uint32 = request.component_bits};
    cmdbuf.clearColorImage(GetImage(), vk::ImageLayout::eTransferDstOptimal, color, vk_range);
}

void Image::SetBackingSamples(u32 num_samples, bool copy_backing) {
    if (!backing || backing->num_samples == num_samples) {
        return;
    }
    ASSERT_MSG(!info.props.is_depth, "Swapping samples is only valid for color images");
    BackingImage* new_backing;
    auto it = std::ranges::find(backing_images, num_samples, &BackingImage::num_samples);
    if (it == backing_images.end()) {
        auto new_image_ci = backing->image->CreateInfo();
        new_image_ci.samples = LiverpoolToVK::NumSamples(num_samples, supported_samples);

        new_backing = &backing_images.emplace_back();
        new_backing->sync_state = ImageSyncState{resource_desc.resources};
        new_backing->num_samples = num_samples;
        new_backing->image = std::make_unique<Vulkan::ImageResource>(instance->GetAllocator());
        new_backing->image->Create(new_image_ci);

        Vulkan::SetObjectName(instance->GetDevice(), new_backing->image->Handle(),
                              "Image {}x{}x{} {} {} {:#x}:{:#x} L:{} M:{} S:{} (backing)",
                              info.size.width, info.size.height, info.size.depth,
                              AmdGpu::NameOf(info.tile_mode), vk::to_string(info.pixel_format),
                              info.guest_address, info.guest_size, info.resources.layers,
                              info.resources.levels, num_samples);
    } else {
        new_backing = std::addressof(*it);
    }

    if (copy_backing) {
        scheduler->EndRendering();
        ASSERT(info.resources.levels == 1 && info.resources.layers == 1);

        // Transition current backing to shader read layout
        auto barriers = GetBarriers(
            {ImageLayout::ShaderReadOnly, ImageAccess::ShaderRead, ImageStage::FragmentShader},
            std::nullopt);

        // Transition dest backing to color attachment layout, not caring of previous contents
        constexpr auto dst_stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        constexpr auto dst_access = vk::AccessFlagBits2::eColorAttachmentWrite;
        constexpr auto dst_layout = vk::ImageLayout::eColorAttachmentOptimal;
        barriers.push_back(vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = dst_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = new_backing->image->Handle(),
            .subresourceRange{
                .aspectMask = aspect_mask,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = info.resources.layers,
            },
        });
        const auto cmdbuf = scheduler->CommandBuffer();
        cmdbuf.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
            .pImageMemoryBarriers = barriers.data(),
        });

        // Copy between ms and non ms backing images
        blit_helper->CopyBetweenMsImages(
            info.size.width, info.size.height, new_backing->num_samples, info.pixel_format,
            backing->num_samples > 1, backing->image->Handle(), new_backing->image->Handle());

        // Update current layout in tracker to new backings layout
        new_backing->sync_state.SetCurrent({ImageLayout::ColorAttachment,
                                            ImageAccess::ColorAttachmentWrite,
                                            ImageStage::ColorAttachmentOutput});
    }

    backing = new_backing;
}

} // namespace VideoCore
