// SPDX-License-Identifier: GPL-2.0-or-later
// port: prints the caller chain as executable offsets, for llvm-symbolizer
// (tools/fault_stacks.py style: theorder-probe.exe+0x...). Diagnostics only.
#pragma once

#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
extern "C" char __ImageBase; // PE image base (linker-defined)
#endif

inline void PortPrintBacktrace(const char* tag) {
#ifdef _WIN32
    void* frames[24];
    const unsigned short count = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
    const auto base = reinterpret_cast<uintptr_t>(&__ImageBase);
    std::printf("Backtrace (%s):", tag);
    for (unsigned short i = 0; i < count; ++i) {
        const auto address = reinterpret_cast<uintptr_t>(frames[i]);
        if (address >= base && address - base < 0x10000000) {
            std::printf(" +%#llx", static_cast<unsigned long long>(address - base));
        }
    }
    std::printf("\n");
    std::fflush(stdout);
#else
    (void)tag;
#endif
}
