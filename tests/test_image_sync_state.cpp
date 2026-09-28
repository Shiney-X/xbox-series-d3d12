// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "video_core/texture_cache/image_sync_state.h"

namespace {

using VideoCore::ImageAccess;
using VideoCore::ImageLayout;
using VideoCore::ImageResourceState;
using VideoCore::ImageStage;
namespace ImageStates = VideoCore::ImageStates;
using VideoCore::ImageSyncState;
using VideoCore::SubresourceRange;

TEST(ImageSyncState, TracksFullPartialAndRepeatedWriteTransitions) {
    ImageSyncState sync{{2, 2}};
    const ImageResourceState read = ImageStates::ShaderReadOnly;
    const ImageResourceState write = ImageStates::TransferDestination;

    auto full = sync.Transition(read);
    ASSERT_EQ(full.size(), 1);
    EXPECT_TRUE(full[0].whole_resource);
    EXPECT_EQ(full[0].before.layout, ImageLayout::Undefined);
    EXPECT_EQ(full[0].after, read);
    EXPECT_TRUE(sync.Transition(read).empty());

    const SubresourceRange selected{{1, 0}, {1, 1}};
    auto partial = sync.Transition(write, selected);
    ASSERT_EQ(partial.size(), 1);
    EXPECT_FALSE(partial[0].whole_resource);
    EXPECT_EQ(partial[0].range, selected);
    EXPECT_EQ(partial[0].before, read);

    auto repeated_write = sync.Transition(write, selected);
    ASSERT_EQ(repeated_write.size(), 1);
    EXPECT_EQ(repeated_write[0].before, write);
    EXPECT_EQ(repeated_write[0].after, write);

    auto restore = sync.Transition(read);
    ASSERT_EQ(restore.size(), 1);
    EXPECT_EQ(restore[0].range, selected);
    EXPECT_EQ(restore[0].before, write);
    EXPECT_TRUE(sync.Transition(read).empty());
}

TEST(ImageSyncState, StageOnlyChangeDoesNotEmitBarrier) {
    ImageSyncState sync;
    const ImageResourceState graphics_read = ImageStates::ShaderReadOnly;
    const ImageResourceState fragment_read{ImageLayout::ShaderReadOnly, ImageAccess::ShaderRead,
                                           ImageStage::FragmentShader};
    ASSERT_EQ(sync.Transition(graphics_read).size(), 1);
    EXPECT_TRUE(sync.Transition(fragment_read).empty());
    EXPECT_EQ(sync.Current(), fragment_read);
}

TEST(ImageSyncState, NamedTransitionRequestsPreserveAccessAndStage) {
    EXPECT_EQ(ImageStates::TransferSource,
              (ImageResourceState{ImageLayout::TransferSource, ImageAccess::TransferRead,
                                  ImageStage::Transfer}));
    EXPECT_EQ(ImageStates::TransferDestination,
              (ImageResourceState{ImageLayout::TransferDestination, ImageAccess::TransferWrite,
                                  ImageStage::Transfer}));
    EXPECT_EQ(ImageStates::GeneralShaderTransferRead,
              (ImageResourceState{ImageLayout::General,
                                  ImageAccess::ShaderRead | ImageAccess::TransferRead,
                                  ImageStage::GraphicsAndCompute}));
}

} // namespace
