// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/amdgpu/resource.h"
#include "video_core/texture_cache/types.h"

namespace VideoCore {

// Guest-visible image view geometry and component selection. Native image
// formats and image-view handles belong to the active renderer.
struct ImageViewDesc {
    AmdGpu::ImageType type = AmdGpu::ImageType::Color2D;
    SubresourceRange range{};
    AmdGpu::CompMapping mapping = AmdGpu::IdentityMapping;
    u32 min_lod = 0;
    bool is_storage = false;

    bool operator==(const ImageViewDesc&) const = default;
};

} // namespace VideoCore
