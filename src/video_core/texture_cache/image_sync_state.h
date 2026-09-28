// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "video_core/texture_cache/types.h"

namespace VideoCore {

// Semantic image states; native layouts, access masks, and stages are adapter details.
enum class ImageLayout : u8 {
    Undefined,
    General,
    TransferSource,
    TransferDestination,
    ShaderReadOnly,
    ColorAttachment,
    DepthAttachment,
    DepthStencilAttachment,
    DepthReadOnly,
    DepthStencilReadOnly,
    DepthReadOnlyStencilAttachment,
    AttachmentFeedbackLoop,
};

enum class ImageAccess : u32 {
    None = 0,
    MemoryRead = 1u << 0,
    MemoryWrite = 1u << 1,
    TransferRead = 1u << 2,
    TransferWrite = 1u << 3,
    ShaderRead = 1u << 4,
    ShaderWrite = 1u << 5,
    ColorAttachmentRead = 1u << 6,
    ColorAttachmentWrite = 1u << 7,
    DepthStencilRead = 1u << 8,
    DepthStencilWrite = 1u << 9,
};

constexpr ImageAccess operator|(ImageAccess left, ImageAccess right) noexcept {
    return static_cast<ImageAccess>(static_cast<u32>(left) | static_cast<u32>(right));
}

constexpr bool HasImageAccess(ImageAccess flags, ImageAccess access) noexcept {
    return (static_cast<u32>(flags) & static_cast<u32>(access)) != 0;
}

enum class ImageStage : u8 {
    AllCommands,
    Transfer,
    GraphicsAndCompute,
    FragmentShader,
    ColorAttachmentOutput,
    Copy,
};

struct ImageResourceState {
    ImageLayout layout = ImageLayout::Undefined;
    ImageAccess access = ImageAccess::None;
    ImageStage stage = ImageStage::AllCommands;

    bool operator==(const ImageResourceState&) const = default;
};

namespace ImageStates {
inline constexpr ImageResourceState TransferSource{ImageLayout::TransferSource,
                                                   ImageAccess::TransferRead, ImageStage::Transfer};
inline constexpr ImageResourceState TransferDestination{
    ImageLayout::TransferDestination, ImageAccess::TransferWrite, ImageStage::Transfer};
inline constexpr ImageResourceState ShaderReadOnly{
    ImageLayout::ShaderReadOnly, ImageAccess::ShaderRead, ImageStage::GraphicsAndCompute};
inline constexpr ImageResourceState GeneralShaderTransferRead{
    ImageLayout::General, ImageAccess::ShaderRead | ImageAccess::TransferRead,
    ImageStage::GraphicsAndCompute};
} // namespace ImageStates

struct ImageTransition {
    ImageResourceState before;
    ImageResourceState after;
    SubresourceRange range;
    bool whole_resource;
};

using ImageTransitions = boost::container::small_vector<ImageTransition, 32>;

class ImageSyncState {
public:
    explicit ImageSyncState(SubresourceExtent extent = {}) : extent{extent} {}

    const ImageResourceState& Current() const noexcept {
        return current;
    }

    // Mirrors the existing backing-image update after an external copy pass.
    void SetCurrent(ImageResourceState state) noexcept {
        current = state;
    }

    [[nodiscard]] ImageTransitions Transition(
        ImageResourceState next, std::optional<SubresourceRange> requested = std::nullopt) {
        const bool partial =
            requested && (requested->base != SubresourceBase{} || requested->extent != extent);
        ImageTransitions transitions;
        if (partial || !subresources.empty()) {
            if (subresources.empty()) {
                subresources.resize(extent.levels * extent.layers, current);
            }
            const auto first_mip = partial ? requested->base.level : 0u;
            const auto mip_count = partial ? requested->extent.levels : extent.levels;
            const auto first_layer = partial ? requested->base.layer : 0u;
            const auto layer_count = partial ? requested->extent.layers : extent.layers;
            for (u32 mip = first_mip; mip < first_mip + mip_count; ++mip) {
                for (u32 layer = first_layer; layer < first_layer + layer_count; ++layer) {
                    auto& state = subresources.at(mip * extent.layers + layer);
                    if (NeedsBarrier(state, next)) {
                        transitions.push_back({state, next, {{mip, layer}, {1, 1}}, false});
                        state = next;
                    }
                }
            }
            if (!partial) {
                subresources.clear();
            }
        } else if (NeedsBarrier(current, next)) {
            transitions.push_back({current, next, {{0, 0}, extent}, true});
        }
        current = next;
        return transitions;
    }

private:
    static bool NeedsBarrier(ImageResourceState before, ImageResourceState after) noexcept {
        // Preserve the existing Vulkan tracker's write-after-* rule. Attachment writes are
        // intentionally not included here; changing that policy requires a separate review.
        const bool was_write = HasImageAccess(before.access, ImageAccess::TransferWrite) ||
                               HasImageAccess(before.access, ImageAccess::ShaderWrite) ||
                               HasImageAccess(before.access, ImageAccess::MemoryWrite);
        return before.layout != after.layout || before.access != after.access || was_write;
    }

    SubresourceExtent extent;
    ImageResourceState current{};
    std::vector<ImageResourceState> subresources;
};

} // namespace VideoCore
