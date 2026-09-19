// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "frontend/window.h"

namespace {

class TestWindow final : public Frontend::Window {
public:
    TestWindow(s32 width, s32 height) : width{width}, height{height} {}

    s32 GetWidth() const override {
        return width;
    }

    s32 GetHeight() const override {
        return height;
    }

    bool IsOpen() const override {
        return true;
    }

    Frontend::WindowSystemInfo GetWindowInfo() const override {
        return {};
    }

    void* GetFrontendHandle() const override {
        return nullptr;
    }

    void SetIcon(std::span<const u8>) override {}
    void WaitEvent() override {}
    void InitTimers() override {}
    void RequestKeyboard() override {}
    void ReleaseKeyboard() override {}

private:
    s32 width;
    s32 height;
};

TEST(FrontendWindow, FactoryDoesNotRequireSdl) {
    Frontend::WindowFactory factory = [](s32 width, s32 height, Input::GameControllers*,
                                         std::string_view) -> std::unique_ptr<Frontend::Window> {
        return std::make_unique<TestWindow>(width, height);
    };

    auto window = factory(1280, 720, nullptr, "test");

    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->GetWidth(), 1280);
    EXPECT_EQ(window->GetHeight(), 720);
    EXPECT_EQ(window->GetWindowInfo().type, Frontend::WindowSystemType::Headless);
    EXPECT_EQ(window->GetFrontendHandle(), nullptr);
}

} // namespace
