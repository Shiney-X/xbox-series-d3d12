// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include "core/uwp/guest_loader_fixture.h"

namespace Core::Uwp {
inline constexpr std::uint32_t FixtureBridgeFrame = 296;
inline constexpr std::size_t FixtureFaultOffset = 0x180;

[[nodiscard]] inline std::vector<std::uint8_t> MakeGuestExecutionFixture() {
    auto file = MakeGuestLoaderFixture();
    // Authored SysV leaf function, not Orbis main: verify RSP%16==8,
    // use the 128-byte red zone, return RDI+RSI, clobber RSI/RDI/XMM6.
    constexpr std::array<std::uint8_t, 39> code{
        0x48, 0x89, 0xe0, 0x83, 0xe0, 0x0f, 0x83, 0xf8, 0x08, 0x75, 0x16, 0x48, 0x89,
        0x7c, 0x24, 0x80, 0x48, 0x8b, 0x44, 0x24, 0x80, 0x48, 0x01, 0xf0, 0x66, 0x0f,
        0xef, 0xf6, 0x31, 0xff, 0x31, 0xf6, 0xc3, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xc3};
    std::copy(code.begin(), code.end(), file.begin() + 0x100);
    // mov rax,[rdi]; ret. Used only with the owned uncommitted guard address.
    constexpr std::array<std::uint8_t, 4> fault{0x48, 0x8b, 0x07, 0xc3};
    std::copy(fault.begin(), fault.end(), file.begin() + FixtureFaultOffset);
    for (unsigned i = 0; i < 8; ++i) {
        file[96 + i] = static_cast<std::uint8_t>(std::uint64_t{0x100} >> (8 * i));
        file[104 + i] = file[96 + i];
    }
    return file;
}

struct FixtureBridge {
    std::vector<std::uint8_t> code;
    std::size_t recovery_offset{};
};

// Win64 signature: uint64(a, b, entry, saved_rsp*).
// Save Win64-only nonvolatile RSI/RDI and XMM6..15 around a SysV leaf call.
// Other nonvolatile GPRs are common to both ABIs; guest must preserve them.
[[nodiscard]] inline FixtureBridge MakeFixtureBridge() {
    FixtureBridge bridge;
    auto& code = bridge.code;
    code = {0x57, 0x56,                          // push rdi; push rsi
            0x49, 0x89, 0x21,                    // mov [r9],rsp (before frame allocation)
            0x48, 0x81, 0xec, 0x28, 0x01, 0, 0}; // sub rsp,296: call alignment
    const auto xmm = [&](bool load) {
        for (unsigned reg = 6; reg < 16; ++reg) {
            code.push_back(0xf3); // movdqu: no dependency on SIMD slot alignment.
            if (reg >= 8)
                code.push_back(0x44); // REX.R
            code.push_back(0x0f);
            code.push_back(load ? 0x6f : 0x7f);
            code.push_back(static_cast<std::uint8_t>(0x84 | ((reg & 7) << 3)));
            code.push_back(0x24);
            const auto offset = 128 + (reg - 6) * 16;
            for (unsigned i = 0; i < 4; ++i)
                code.push_back(static_cast<std::uint8_t>(offset >> (8 * i)));
        }
    };
    xmm(false);
    code.insert(code.end(), {0xfc,               // cld
                             0x48, 0x89, 0xcf,   // mov rdi,rcx
                             0x48, 0x89, 0xd6,   // mov rsi,rdx
                             0x41, 0xff, 0xd0}); // call r8
    bridge.recovery_offset = code.size();
    xmm(true);
    code.insert(code.end(), {0xfc, 0x48, 0x81, 0xc4, 0x28, 0x01, 0, 0, 0x5e, 0x5f,
                             0xc3}); // cld; add rsp,296; pop rsi/rdi; ret
    return bridge;
}
} // namespace Core::Uwp
