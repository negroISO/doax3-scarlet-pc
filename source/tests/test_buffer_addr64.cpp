// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader_recompiler/frontend/translate/translate.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/resource.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace Shader::Optimization {
IR::U32 CalculateBufferAddress(IR::IREmitter&, const IR::Inst&, const Info&, const AmdGpu::Buffer&,
                               u32);
}

int main() {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    using namespace Shader;
    for (auto opcode : {Gcn::Opcode::BUFFER_LOAD_DWORD, Gcn::Opcode::BUFFER_STORE_DWORD,
                        Gcn::Opcode::BUFFER_ATOMIC_ADD}) {
        for (bool addr64 : {false, true}) {
            Info info{};
            RuntimeInfo runtime{};
            runtime.Initialize(HwStage::Compute, SwStage::Compute);
            Profile profile{};
            Common::ObjectPool<IR::Inst> pool;
            IR::Block block(pool);
            Gcn::GcnInst inst{};
            inst.opcode = opcode;
            inst.category = Gcn::InstCategory::VectorMemory;
            inst.control.mubuf.addr64 = addr64;
            inst.src[0].code = 6;
            inst.src[1].code = 10;
            inst.src[2].code = 0;
            inst.src[3].field = Gcn::OperandField::ConstZero;
            Gcn::Translator translator(info, runtime, profile);
            translator.Translate(&block, 0, IR::Condition::True, std::span{&inst, 1});
            bool found{};
            for (auto& emitted : block.Instructions()) {
                if (!Optimization::IsBufferInstruction(emitted))
                    continue;
                found = true;
                assert(bool(emitted.Flags<IR::BufferInstInfo>().raw & (1ULL << 25)) == addr64);
                auto* address = emitted.Arg(1).Inst();
                for (u32 component = 0; component < 2; ++component) {
                    const auto value = address->Arg(component);
                    if (addr64) {
                        assert(value.Inst()->GetOpcode() == IR::Opcode::GetVectorRegister);
                        assert(value.Inst()->Arg(0).VectorReg() == IR::VectorReg(6 + component));
                    } else {
                        assert(value.IsImmediate() && value.U32() == 0);
                    }
                }
            }
            assert(found);
        }
    }
    for (u32 width : {8u, 16u, 32u, 64u}) {
        for (u32 lo : {0u, 12u, 0xfffffffcu}) {
            for (u32 hi : {0u, 1u, 3u}) {
                for (u32 soffset : {0u, 16u, 0xffffffffu}) {
                    Info info{};
                    Common::ObjectPool<IR::Inst> pool;
                    IR::Block block(pool);
                    IR::IREmitter ir(block);
                    IR::BufferInstInfo flags{};
                    flags.raw = 1ULL << 25; // addr64; bit 24 is already sharp_source.
                    flags.inst_offset.Assign(12);
                    const auto address =
                        ir.CompositeConstruct(ir.Imm32(lo), ir.Imm32(hi), ir.Imm32(soffset));
                    IR::Value load;
                    if (width == 8)
                        load = ir.LoadBufferU8(ir.Imm32(0), address, flags);
                    if (width == 16)
                        load = ir.LoadBufferU16(ir.Imm32(0), address, flags);
                    if (width == 32)
                        load = ir.LoadBufferU32(1, ir.Imm32(0), address, flags);
                    if (width == 64)
                        load = ir.LoadBufferU64(ir.Imm32(0), address, flags);
                    AmdGpu::Buffer buffer{};
                    buffer.stride = 48; // addr64 ignores stride and idxen/offen.
                    auto result =
                        Optimization::CalculateBufferAddress(ir, *load.Inst(), info, buffer, 48);
                    ir.SetScalarReg(IR::ScalarReg::S0, result);
                    auto& sink = block.Instructions().back();
                    IR::BlockList blocks{&block};
                    Optimization::ConstantPropagationPass(blocks);
                    const u32 shift = width == 8 ? 0 : width == 16 ? 1 : width == 32 ? 2 : 3;
                    const u64 bytes = (u64{hi} << 32 | lo) + soffset + 12;
                    const u32 expected = static_cast<u32>(bytes >> shift);
                    assert(sink.Arg(1).IsImmediate());
                    if (sink.Arg(1).U32() != expected) {
                        std::fprintf(stderr,
                                     "addr64 width=%u lo=%x hi=%x soffset=%x: got %x expected %x\n",
                                     width, lo, hi, soffset, sink.Arg(1).U32(), expected);
                        return 1;
                    }
                }
            }
        }
    }
    std::puts("addr64: 6 frontend and 108 address/carry/element-width cases passed");
}
