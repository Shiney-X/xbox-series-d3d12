// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <variant>

#include "core/libraries/videoout/buffer.h"
#include "video_core/amdgpu/pixel_format.h"
#include "video_core/amdgpu/regs_depth.h"

namespace VideoCore {

// Preserve the guest's format identity before a renderer chooses its host format.
struct SurfaceImageFormat {
    AmdGpu::DataFormat data;
    AmdGpu::NumberFormat number;
    bool reinterpret_as_depth = false;
};

struct DepthImageFormat {
    AmdGpu::DepthBuffer::ZFormat depth;
    AmdGpu::DepthBuffer::StencilFormat stencil;
};

struct VideoOutImageFormat {
    Libraries::VideoOut::PixelFormat pixel;
};

// monostate covers default-constructed auxiliary images without a guest format.
using ImageFormatDesc =
    std::variant<std::monostate, SurfaceImageFormat, DepthImageFormat, VideoOutImageFormat>;

} // namespace VideoCore
