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

    bool operator==(const SurfaceImageFormat&) const = default;
};

struct DepthImageFormat {
    AmdGpu::DepthBuffer::ZFormat depth;
    AmdGpu::DepthBuffer::StencilFormat stencil;

    bool operator==(const DepthImageFormat&) const = default;
};

struct VideoOutImageFormat {
    Libraries::VideoOut::PixelFormat pixel;

    bool operator==(const VideoOutImageFormat&) const = default;
};

// monostate covers default-constructed auxiliary images without a guest format.
using ImageFormatDesc =
    std::variant<std::monostate, SurfaceImageFormat, DepthImageFormat, VideoOutImageFormat>;

} // namespace VideoCore
