// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace VideoCore {
class Buffer;
enum class BufferAccess : u8;
struct BufferTransition;
} // namespace VideoCore

namespace Vulkan {

class BufferResource;

// Translates a semantic buffer transition into a Vulkan barrier.
[[nodiscard]] std::optional<vk::BufferMemoryBarrier2> GetBufferBarrier(VideoCore::Buffer& buffer,
                                                                       VideoCore::BufferAccess next,
                                                                       u32 offset = 0);

[[nodiscard]] vk::BufferMemoryBarrier2 ToBufferBarrier(
    const BufferResource& resource, const VideoCore::BufferTransition& transition);

} // namespace Vulkan
