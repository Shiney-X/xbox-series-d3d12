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
    if (!staged.plan.valid || !staged.error.empty() || staged.bytes.size() != 32768 ||
        staged.bytes[0x100] != 0x48 || staged.bytes[0x180] != 0x48 || staged.bytes[0x181] != 0x8b ||
        bridge.code.size() != 229 || bridge.epilogue_offset != 120 || bridge.code.back() != 0xc3) {
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
