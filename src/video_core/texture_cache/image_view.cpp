// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/resource.h"
#include "video_core/amdgpu/regs_color.h"
#include "video_core/renderer_vulkan/vk_image_view_resource.h"
#include "video_core/texture_cache/image_view.h"

namespace VideoCore {

ImageViewInfo::ImageViewInfo(const AmdGpu::Image& image,
                             const Shader::ImageResource& desc) noexcept {
    is_storage = desc.is_written;
    guest_format = SurfaceImageFormat{image.GetDataFmt(), image.GetNumberFmt(), desc.is_depth};

    range.base.level = image.base_level;
    range.base.layer = image.base_array;
    range.extent.levels = image.NumViewLevels(desc.is_array);
    range.extent.layers = image.NumViewLayers(desc.is_array);
    type = image.GetViewType(desc.is_array);
    min_lod = static_cast<u32>(image.min_lod);

    if (!is_storage) {
        mapping = image.DstSelect();
    }
}

ImageViewInfo::ImageViewInfo(const AmdGpu::ColorBuffer& col_buffer) noexcept {
    range.base.layer = col_buffer.BaseSlice();
    range.extent.layers = col_buffer.NumSlices() - range.base.layer;
    type = range.extent.layers > 1 ? AmdGpu::ImageType::Color2DArray : AmdGpu::ImageType::Color2D;
    guest_format = SurfaceImageFormat{col_buffer.GetDataFmt(), col_buffer.GetNumberFmt()};
}

ImageViewInfo::ImageViewInfo(const AmdGpu::DepthBuffer& depth_buffer, AmdGpu::DepthView view,
                             AmdGpu::DepthControl ctl) {
    guest_format = DepthImageFormat{depth_buffer.z_info.format, depth_buffer.stencil_info.format};
    is_storage = ctl.depth_write_enable;
    range.base.layer = view.slice_start;
    range.extent.layers = view.NumSlices() - range.base.layer;
    type = range.extent.layers > 1 ? AmdGpu::ImageType::Color2DArray : AmdGpu::ImageType::Color2D;
}

ImageView::ImageView(const Vulkan::Instance& instance, const ImageViewInfo& info_,
                     const Image& image)
    : info{info_}, native{std::make_unique<Vulkan::ImageViewResource>(instance, info_, image)} {}

ImageView::~ImageView() = default;
ImageView::ImageView(ImageView&&) = default;
ImageView& ImageView::operator=(ImageView&&) = default;

const Vulkan::ImageViewResource& ImageView::Native() const noexcept {
    return *native;
}

} // namespace VideoCore
