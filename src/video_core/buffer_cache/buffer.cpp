// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/alignment.h"
#include "common/assert.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_vulkan/vk_buffer_resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace VideoCore {

namespace {

struct VulkanBufferAccess {
    vk::AccessFlags2 access;
    vk::PipelineStageFlagBits2 stage;
};

constexpr VulkanBufferAccess ToVulkanAccess(BufferAccess access) {
    switch (access) {
    case BufferAccess::Initial:
        return {vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite |
                    vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eAllCommands};
    case BufferAccess::General:
        return {vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                vk::PipelineStageFlagBits2::eAllCommands};
    case BufferAccess::VertexRead:
        return {vk::AccessFlagBits2::eVertexAttributeRead,
                vk::PipelineStageFlagBits2::eVertexAttributeInput};
    case BufferAccess::IndexRead:
        return {vk::AccessFlagBits2::eIndexRead, vk::PipelineStageFlagBits2::eIndexInput};
    case BufferAccess::IndirectRead:
        return {vk::AccessFlagBits2::eIndirectCommandRead,
                vk::PipelineStageFlagBits2::eDrawIndirect};
    case BufferAccess::ShaderRead:
        return {vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eAllCommands};
    case BufferAccess::ShaderWrite:
        return {vk::AccessFlagBits2::eShaderWrite, vk::PipelineStageFlagBits2::eAllCommands};
    case BufferAccess::TransferRead:
        return {vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eTransfer};
    case BufferAccess::TransferWrite:
        return {vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eTransfer};
    }
    UNREACHABLE();
}

} // namespace

std::string_view BufferTypeName(MemoryUsage type) {
    switch (type) {
    case MemoryUsage::Upload:
        return "Upload";
    case MemoryUsage::Download:
        return "Download";
    case MemoryUsage::Stream:
        return "Stream";
    case MemoryUsage::DeviceLocal:
        return "DeviceLocal";
    default:
        return "Invalid";
    }
}

Buffer::Buffer(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_, BufferDesc desc)
    : cpu_addr{desc.guest_address}, size_bytes{desc.size_bytes}, instance{&instance_},
      scheduler{&scheduler_}, usage{desc.memory_usage},
      buffer{std::make_unique<Vulkan::BufferResource>(instance_, desc)} {

    const auto device = instance->GetDevice();
    Vulkan::SetObjectName(device, Handle(), "Buffer {:#x}:{:#x}", cpu_addr, size_bytes);

    // Map it if it is host visible.
    mapped_data = buffer->MappedData();
    is_coherent = buffer->IsCoherent();
}

Buffer::~Buffer() = default;
Buffer::Buffer(Buffer&&) noexcept = default;
Buffer& Buffer::operator=(Buffer&&) noexcept = default;

const Vulkan::BufferResource& Buffer::Native() const noexcept {
    return *buffer;
}

vk::Buffer Buffer::Handle() const noexcept {
    return Native().Handle();
}

vk::DeviceAddress Buffer::BufferDeviceAddress() const noexcept {
    const auto address = Native().DeviceAddress();
    ASSERT_MSG(address != 0, "Can't get BDA from a non BDA buffer");
    return address;
}

std::optional<vk::BufferMemoryBarrier2> Buffer::GetBarrier(BufferAccess next, u32 offset) {
    const auto transition = sync_state.Transition(next, offset, size_bytes);
    if (!transition) {
        return std::nullopt;
    }

    const auto source = ToVulkanAccess(transition->before);
    const auto destination = ToVulkanAccess(transition->after);
    return vk::BufferMemoryBarrier2{
        .srcStageMask = source.stage,
        .srcAccessMask = source.access,
        .dstStageMask = destination.stage,
        .dstAccessMask = destination.access,
        .buffer = Handle(),
        .offset = transition->offset,
        .size = transition->size,
    };
}

void Buffer::Fill(u64 offset, u32 num_bytes, u32 value) {
    scheduler->EndRendering();
    ASSERT_MSG(offset % 4 == 0 && num_bytes % 4 == 0,
               "FillBuffer size must be a multiple of 4 bytes");
    const auto cmdbuf = scheduler->CommandBuffer();
    const vk::BufferMemoryBarrier2 pre_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .buffer = Handle(),
        .offset = offset,
        .size = num_bytes,
    };
    const vk::BufferMemoryBarrier2 post_barrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .buffer = Handle(),
        .offset = offset,
        .size = num_bytes,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &pre_barrier,
    });
    cmdbuf.fillBuffer(Handle(), offset, num_bytes, value);
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &post_barrier,
    });
}

constexpr u64 WATCHES_INITIAL_RESERVE = 0x4000;
constexpr u64 WATCHES_RESERVE_CHUNK = 0x1000;

StreamBuffer::StreamBuffer(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                           MemoryUsage usage, u64 size_bytes)
    : Buffer{instance, scheduler, BufferDesc{usage, 0, AllFlags, size_bytes}} {
    ReserveWatches(current_watches, WATCHES_INITIAL_RESERVE);
    ReserveWatches(previous_watches, WATCHES_INITIAL_RESERVE);
    const auto device = instance.GetDevice();
    Vulkan::SetObjectName(device, Handle(), "StreamBuffer({}):{:#x}", BufferTypeName(usage),
                          size_bytes);
}

std::pair<u8*, u64> StreamBuffer::Map(u64 size, u64 alignment, bool allow_wait) {
    if (!is_coherent && usage == MemoryUsage::Stream) {
        size = Common::AlignUp(size, instance->NonCoherentAtomSize());
    }

    if (size > this->size_bytes) {
        return {nullptr, 0};
    }

    mapped_size = size;

    if (alignment > 0) {
        offset = Common::AlignUp(offset, alignment);
    }

    if (offset + size > this->size_bytes) {
        // The buffer would overflow, save the amount of used watches and reset the state.
        invalidation_mark = current_watch_cursor;
        current_watch_cursor = 0;
        offset = 0;

        // Swap watches and reset waiting cursors.
        std::swap(previous_watches, current_watches);
        wait_cursor = 0;
        wait_bound = 0;
    }

    const u64 mapped_upper_bound = offset + size;
    if (!WaitPendingOperations(mapped_upper_bound, allow_wait)) {
        return {nullptr, 0};
    }

    return {mapped_data.data() + offset, offset};
}

void StreamBuffer::Commit() {
    if (!is_coherent) {
        if (usage == MemoryUsage::Download) {
            Native().InvalidateMappedRange(offset, mapped_size);
        } else {
            Native().FlushMappedRange(offset, mapped_size);
        }
    }

    offset += mapped_size;
    if (current_watch_cursor != 0 &&
        current_watches[current_watch_cursor].tick == scheduler->CurrentTick()) {
        current_watches[current_watch_cursor].upper_bound = offset;
        return;
    }

    if (current_watch_cursor + 1 >= current_watches.size()) {
        // Ensure that there are enough watches.
        ReserveWatches(current_watches, WATCHES_RESERVE_CHUNK);
    }

    auto& watch = current_watches[current_watch_cursor++];
    watch.upper_bound = offset;
    watch.tick = scheduler->CurrentTick();
}

void StreamBuffer::ReserveWatches(std::vector<Watch>& watches, std::size_t grow_size) {
    watches.resize(watches.size() + grow_size);
}

bool StreamBuffer::WaitPendingOperations(u64 requested_upper_bound, bool allow_wait) {
    if (!invalidation_mark) {
        return true;
    }
    while (requested_upper_bound > wait_bound && wait_cursor < *invalidation_mark) {
        auto& watch = previous_watches[wait_cursor];
        if (!scheduler->IsFree(watch.tick) && !allow_wait) {
            return false;
        }
        scheduler->Wait(watch.tick);
        wait_bound = watch.upper_bound;
        ++wait_cursor;
    }
    return true;
}

} // namespace VideoCore
