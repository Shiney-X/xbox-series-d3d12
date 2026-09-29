// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "common/types.h"
#include "video_core/amdgpu/tiling.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image_buffer_copy.h"

namespace VideoCore {
struct ImageInfo;
struct Image;
class StreamBuffer;
} // namespace VideoCore

namespace Vulkan {

class BufferResource;
class Instance;
class Scheduler;

class TileManager {
    static constexpr size_t NUM_BPPS = 5;

public:
    using ScratchBuffer = std::shared_ptr<BufferResource>;
    using Result = std::pair<const BufferResource*, u32>;

    explicit TileManager(const Instance& instance, Scheduler& scheduler,
                         VideoCore::StreamBuffer& stream_buffer);
    ~TileManager();

    void TileImage(VideoCore::Image& in_image, std::span<VideoCore::ImageBufferCopy> buffer_copies,
                   const BufferResource& out_buffer, u32 out_offset, u32 copy_size);

    Result DetileImage(const BufferResource& in_buffer, u32 in_offset,
                       const VideoCore::ImageInfo& info);

private:
    vk::Pipeline GetTilingPipeline(const VideoCore::ImageInfo& info, bool is_tiler);
    ScratchBuffer GetScratchBuffer(u32 size);

private:
    const Instance& instance;
    Scheduler& scheduler;
    VideoCore::StreamBuffer& stream_buffer;
    vk::UniqueDescriptorSetLayout desc_layout;
    vk::UniquePipelineLayout pl_layout;
    std::array<vk::UniquePipeline, AmdGpu::NUM_TILE_MODES * NUM_BPPS> detilers{};
    std::array<vk::UniquePipeline, AmdGpu::NUM_TILE_MODES * NUM_BPPS> tilers{};
};

} // namespace Vulkan
