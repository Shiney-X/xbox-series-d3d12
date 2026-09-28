// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/texture_cache/types.h"

namespace VideoCore {

// A buffer/image transfer request. API-specific aspects and copy regions belong
// to the renderer adapter, not to the cache that describes guest data.
struct ImageBufferCopy {
    u64 buffer_offset = 0;
    u32 buffer_row_length = 0;
    u32 buffer_image_height = 0;
    u32 mip_level = 0;
    u32 base_layer = 0;
    u32 layer_count = 1;
    Offset3D image_offset{};
    Extent3D image_extent{};

    [[nodiscard]] constexpr bool Valid() const noexcept {
        return layer_count != 0 && image_extent.width != 0 && image_extent.height != 0 &&
               image_extent.depth != 0;
    }
};

} // namespace VideoCore
