// SPDX-FileCopyrightText: 2026 Shiney-X and xbox-series-d3d12 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include <stdexcept>
#include "core/uwp/guest_execution_fixture.h"
#if defined(__linux__) && defined(__x86_64__)
#include <cstring>
#include <sys/mman.h>
#endif

int main() {
    using namespace Core::Uwp;
    const auto staged = StageRawGuest(MakeGuestExecutionFixture());
    const auto bridge = MakeFixtureBridge();
    constexpr std::array<std::uint8_t, 52> expected_unwind{
        0x01, 0x6e, 0x18, 0x00, 0x6e, 0xf8, 0x11, 0x00, 0x64, 0xe8, 0x10, 0x00, 0x5a,
        0xd8, 0x0f, 0x00, 0x50, 0xc8, 0x0e, 0x00, 0x46, 0xb8, 0x0d, 0x00, 0x3c, 0xa8,
        0x0c, 0x00, 0x32, 0x98, 0x0b, 0x00, 0x28, 0x88, 0x0a, 0x00, 0x1e, 0x78, 0x09,
        0x00, 0x15, 0x68, 0x08, 0x00, 0x0c, 0x01, 0x25, 0x00, 0x02, 0x60, 0x01, 0x70};
    if (!staged.plan.valid || !staged.error.empty() || staged.bytes.size() != 32768 ||
        staged.bytes[0x100] != 0x48 || staged.bytes[0x180] != 0x48 || staged.bytes[0x181] != 0x8b ||
        bridge.code.size() != 229 || bridge.epilogue_offset != 120 || bridge.code.back() != 0xc3 ||
        bridge.unwind.size() != expected_unwind.size() ||
        !std::equal(bridge.unwind.begin(), bridge.unwind.end(), expected_unwind.begin()) ||
        staged.bytes[FixtureSysvFaultInstruction] != 0x49 ||
        staged.bytes[FixtureSysvFaultInstruction + 1] != 0x8b ||
        staged.bytes[FixtureSysvFaultInstruction + 2] != 0x02) {
        std::cerr << "fixture/bridge layout failed\n";
        return 1;
    }
#if defined(__linux__) && defined(__x86_64__)
    // Execute exactly the same Win64->SysV byte bridge, not a game/runtime mock.
    // This does not test Windows API availability, fault recovery or AppContainer.
    void* code = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED)
        return 1;
    auto* bytes = static_cast<std::uint8_t*>(code);
    std::memcpy(bytes, staged.bytes.data(), 512);
    std::memcpy(bytes + 512, bridge.code.data(), bridge.code.size());
    if (mprotect(code, 4096, PROT_READ | PROT_EXEC) != 0) {
        munmap(code, 4096);
        return 1;
    }
    using Bridge = std::uint64_t(__attribute__((ms_abi))*)(std::uint64_t, std::uint64_t,
                                                           const void*, std::uint64_t*);
    std::uint64_t saved_rsp{};
    const auto invoke = reinterpret_cast<Bridge>(bytes + 512);
    const auto first = invoke(19, 23, bytes + 0x100, &saved_rsp);
    const auto second = invoke(7, 11, bytes + 0x100, &saved_rsp);
    munmap(code, 4096);
    if (first != 42 || second != 18 || saved_rsp == 0) {
        std::cerr << "Win64->SysV authored leaf call failed\n";
        return 1;
    }
#endif
    std::cout << "execution fixture/bridge passed; no game execution\n";
    return 0;
}
