// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <type_traits>
#include <gtest/gtest.h>
#include "video_core/gpu_memory_tracker.h"

namespace {

class RecordingGpuMemoryTracker final : public VideoCore::GpuMemoryTracker {
public:
    bool InvalidateMemory(VAddr addr, u64 size) override {
        last_addr = addr;
        last_size = size;
        return invalidate_result;
    }

    bool ReadMemory(VAddr addr, u64 size) override {
        last_addr = addr;
        last_size = size;
        return read_result;
    }

    bool IsMapped(VAddr addr, u64 size) override {
        last_addr = addr;
        last_size = size;
        return mapped_result;
    }

    void MapMemory(VAddr addr, u64 size) override {
        last_addr = addr;
        last_size = size;
        mapped = true;
    }

    void UnmapMemory(VAddr addr, u64 size) override {
        last_addr = addr;
        last_size = size;
        mapped = false;
    }

    VAddr last_addr{};
    u64 last_size{};
    bool mapped{};
    bool invalidate_result{true};
    bool read_result{true};
    bool mapped_result{true};
};

static_assert(std::is_abstract_v<VideoCore::GpuMemoryTracker>);
static_assert(std::has_virtual_destructor_v<VideoCore::GpuMemoryTracker>);

TEST(GpuMemoryTracker, DispatchesThroughBackendNeutralContract) {
    RecordingGpuMemoryTracker backend;
    VideoCore::GpuMemoryTracker& tracker = backend;

    tracker.MapMemory(0x1000, 0x2000);
    EXPECT_TRUE(backend.mapped);
    EXPECT_EQ(backend.last_addr, 0x1000);
    EXPECT_EQ(backend.last_size, 0x2000);

    EXPECT_TRUE(tracker.InvalidateMemory(0x1800, 0x80));
    EXPECT_EQ(backend.last_addr, 0x1800);
    EXPECT_EQ(backend.last_size, 0x80);

    EXPECT_TRUE(tracker.ReadMemory(0x1A00, 0x40));
    EXPECT_EQ(backend.last_addr, 0x1A00);
    EXPECT_EQ(backend.last_size, 0x40);

    EXPECT_TRUE(tracker.IsMapped(0x1000, 0x2000));
    EXPECT_EQ(backend.last_addr, 0x1000);
    EXPECT_EQ(backend.last_size, 0x2000);

    tracker.UnmapMemory(0x1000, 0x2000);
    EXPECT_FALSE(backend.mapped);
}

} // namespace
