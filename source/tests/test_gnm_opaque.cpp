// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cstdio>
#include <span>
#include "core/libraries/gnmdriver/gnmdriver.h"
#include "video_core/amdgpu/pm4_cmds.h"
#undef NDEBUG
#include <cassert>

int main() {
    using Libraries::GnmDriver::sceGnmDrawOpaqueAuto;
    std::array<u32, 9> words;
    words.fill(0x50); // stale command data as observed at Fortune's pool freeze
    assert(sceGnmDrawOpaqueAuto(words.data()+1, 7, 0) == 0);
    assert(words.front() == 0x50 && words.back() == 0x50);
    const auto* draw = reinterpret_cast<const AmdGpu::PM4CmdDrawIndexAuto*>(words.data()+1);
    assert(draw->header.type == 3);
    assert(draw->header.opcode == AmdGpu::PM4ItOpcode::DrawIndexAuto);
    assert(draw->index_count == 0 && draw->draw_initiator == 0x42);
    unsigned offset = 1;
    while (offset < 8) {
        const auto* header = reinterpret_cast<const AmdGpu::PM4Header*>(words.data()+offset);
        assert(header->type == 3);
        offset += header->type3.NumWords()+1;
    }
    assert(offset == 8); // the next packet remains exactly at caller's +7 boundary
    words.fill(0x50);
    assert(sceGnmDrawOpaqueAuto(nullptr, 7, 0) != 0);
    assert(sceGnmDrawOpaqueAuto(words.data()+1, 6, 0) != 0);
    assert(sceGnmDrawOpaqueAuto(words.data()+1, 7, 2) != 0);
    for (u32 word : words) assert(word == 0x50);
    std::puts("PASS: opaque draw fills seven DWORDs, preserves bounds, rejects invalid inputs");
}
