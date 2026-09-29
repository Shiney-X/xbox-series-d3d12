// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "video_core/texture_cache/types.h"

namespace VideoCore {

enum class ImageCopyAspects : u8 {
    Color,
    Depth,
    Stencil,
    DepthStencil,
};

struct ImageCopyRequest {
    ImageCopyAspects aspects = ImageCopyAspects::Color;
    SubresourceBase src{};
    SubresourceBase dst{};
    u32 src_layer_count = 1;
    u32 dst_layer_count = 1;
    Offset3D src_offset{};
    Offset3D dst_offset{};
    Extent3D extent{};

    [[nodiscard]] constexpr bool Valid() const noexcept {
        return src_layer_count != 0 && dst_layer_count != 0 && extent.width != 0 &&
               extent.height != 0 && extent.depth != 0;
    }
};

// Raw component bits preserve integer and floating-point Vulkan clear values.
// Their interpretation is selected by the destination image format.
struct ColorClearRequest {
    std::array<u32, 4> component_bits{};
    SubresourceRange range{};
};

} // namespace VideoCore
