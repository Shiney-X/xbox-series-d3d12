// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/amdgpu/resource.h"
#include "video_core/texture_cache/image_format_desc.h"
#include "video_core/texture_cache/image_usage.h"
#include "video_core/texture_cache/types.h"

namespace VideoCore {

// Allocation request from the texture cache. The active renderer selects the
// host format, memory placement, and API-specific creation flags.
struct ImageResourceDesc {
    ImageFormatDesc guest_format{};
    AmdGpu::ImageType type = AmdGpu::ImageType::Invalid;
    Extent3D size{1, 1, 1};
    SubresourceExtent resources{};
    ImageUsage usage = ImageUsage::None;
    u32 num_samples = 1;
    bool is_volume = false;
    bool is_block = false;
    bool is_depth = false;
    bool has_stencil = false;

    [[nodiscard]] bool Valid() const noexcept {
        return !std::holds_alternative<std::monostate>(guest_format) &&
               type != AmdGpu::ImageType::Invalid && size.width != 0 && size.height != 0 &&
               size.depth != 0 && resources.levels != 0 && resources.layers != 0 &&
               num_samples != 0;
    }
};

} // namespace VideoCore
