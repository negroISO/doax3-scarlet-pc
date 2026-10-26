// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "shader_recompiler/frontend/translate/translate.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/passes/resource_pass.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/runtime_info.h"

int main(int argc, char**) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    using namespace Shader;
    const bool dma = argc == 1;
#ifdef _WIN32
    _putenv_s("THEORDER_DIRECT_MEMORY_ACCESS", dma ? "1" : "0");
#else
    setenv("THEORDER_DIRECT_MEMORY_ACCESS", dma ? "1" : "0", 1);
#endif
    u32 cases{};
    for (bool floating : {false, true}) {
        for (u32 count = 1; count <= 4; ++count) {
            for (u32 lo : {0x12340u, 0xfffffffcu, 1u, 2u, 3u}) {
                for (u32 soffset : {0u, 0xffffffffu}) {
                    Info info{};
                    Profile profile{};
                    Common::ObjectPool<IR::Inst> pool;
                    IR::Block block(pool);
                    IR::IREmitter ir(block);
                    IR::BufferInstInfo flags{};
                    flags.addr64.Assign(true);
                    flags.inst_offset.Assign(12);
                    const auto address =
                        ir.CompositeConstruct(ir.Imm32(lo), ir.Imm32(3u), ir.Imm32(soffset));
                    const auto load = floating ? ir.LoadBufferF32(count, ir.Imm32(0), address, flags)
                                               : ir.LoadBufferU32(count, ir.Imm32(0), address, flags);
                    const auto component = count == 1 ? load : ir.CompositeExtract(load, 0);
                    ir.SetScalarReg(IR::ScalarReg::S0,
                                    floating ? ir.BitCast<IR::U32>(IR::F32{component})
                                             : IR::U32{component});
                    auto& sink = block.Instructions().back();
                    Optimization::ResourceDiscoveryList resources;
                    auto& usage = resources.emplace_back(load.Inst());
                    auto& sharp = usage.sharps[0];
                    sharp.num_dwords = 4;
                    // Literal V# with base zero: VGPRs supply the guest address.
                    sharp.dwords = {ir.Imm32(0u), ir.Imm32(0u), ir.Imm32(0xffffffffu),
                                    ir.Imm32(0x2000c004u)};
                    const auto original_type = load.Type();
                    const auto use = load.Inst()->Uses().front();
                    Optimization::ResourcePatchingPass(info, resources, profile);
                    assert(use.user->Arg(use.operand).Type() == original_type);
                    IR::BlockList blocks{&block};
                    Optimization::ConstantPropagationPass(blocks);
                    u32 reads{};
                    for (const auto& emitted : block.Instructions()) {
                        if (emitted.GetOpcode() != IR::Opcode::ReadConst) {
                            continue;
                        }
                        const u64 expected = ((u64{3} << 32 | lo) + soffset + 12) & ~u64{3};
                        const auto* base = emitted.Arg(0).Inst();
                        assert(base->Arg(0).IsImmediate() && base->Arg(1).IsImmediate());
                        assert(base->Arg(0).U32() == static_cast<u32>(expected));
                        assert(base->Arg(1).U32() == static_cast<u32>(expected >> 32));
                        assert(emitted.Arg(1).U32() == reads++);
                    }
                    if (reads != (dma ? count : 0)) {
                        std::fprintf(stderr, "inline V#: DMA=%d, components=%u: got %u BDA reads\n",
                                     dma, count, reads);
                        return 1;
                    }
                    if (dma) {
                        assert(load.Inst()->GetOpcode() == IR::Opcode::Void);
                        // F32 loads must retain their bitcast, including scalar replacements.
                        assert(sink.Arg(1).Type() == IR::Type::U32);
                    } else {
                        assert(load.Inst()->Type() == original_type);
                    }
                    ++cases;
                }
            }
        }
    }
    std::printf("inline V#: %u raw load/type/carry/alignment cases passed (DMA=%d)\n", cases, dma);

    for (u32 variant = 0; variant < 8; ++variant) {
        Info info{};
        Profile profile{};
        Common::ObjectPool<IR::Inst> pool;
        IR::Block block(pool);
        IR::IREmitter ir(block);
        IR::BufferInstInfo flags{};
        flags.addr64.Assign(variant != 0);
        flags.typed.Assign(variant == 1);
        const auto address = ir.CompositeConstruct(ir.Imm32(0x10000u), ir.Imm32(3u), ir.Imm32(0u));
        const IR::Value load = variant == 6 ? IR::Value{ir.LoadBufferU8(ir.Imm32(0), address, flags)}
                                          : ir.LoadBufferU32(1, ir.Imm32(0), address, flags);
        Optimization::ResourceDiscoveryList resources;
        auto& usage = resources.emplace_back(load.Inst());
        auto& sharp = usage.sharps[0];
        sharp.num_dwords = 4;
        sharp.dwords = {ir.Imm32(variant == 2 ? 0x10000u : 0u),
                        ir.Imm32(variant == 3 ? 0x80000000u : 0u),
                        ir.Imm32(variant == 4 ? 0u : 0xffffffffu),
                        ir.Imm32(variant == 5 ? 0x2080c004u : 0x2000c004u)};
        if (variant == 7) {
            sharp.post_op = SharpFetchPostOp::OffsetByProgramBase;
        }
        const auto original_opcode = load.Inst()->GetOpcode();
        Optimization::ResourcePatchingPass(info, resources, profile);
        assert(load.Inst()->GetOpcode() == original_opcode);
        for (const auto& emitted : block.Instructions()) {
            assert(emitted.GetOpcode() != IR::Opcode::ReadConst);
        }
    }
    std::puts("inline V#: 8 non-pointer/unsupported load cases retain buffer bindings");

    for (auto opcode : {Gcn::Opcode::DS_APPEND, Gcn::Opcode::DS_CONSUME}) {
        for (u32 snapshot : {0xbe800000u, 0xbea00000u}) {
            Info info{};
            std::array<u32, 16> user_data{};
            user_data[3] = snapshot;
            info.user_data = user_data;
            RuntimeInfo runtime{};
            runtime.Initialize(HwStage::Compute, SwStage::Compute);
            Profile profile{};
            Common::ObjectPool<IR::Inst> pool;
            IR::Block block(pool);
            IR::IREmitter ir(block);
            Gcn::GcnInst inst{};
            inst.opcode = opcode;
            inst.category = Gcn::InstCategory::DataShare;
            inst.control.ds.offset0 = 12;
            inst.dst[0].field = Gcn::OperandField::VectorGPR;
            inst.dst[0].code = 1;
            Gcn::Translator translator(info, runtime, profile);
            translator.Translate(&block, 0, IR::Condition::True, std::span{&inst, 1});
            IR::IREmitter prelude(block, block.Instructions().begin());
            const auto dynamic_base = prelude.GetUserData(IR::ScalarReg::S3);
            prelude.SetM0(dynamic_base);
            prelude.SetExec(prelude.Imm1(true));
            IR::Program program(info);
            program.blocks.push_back(&block);
            program.post_order_blocks.push_back(&block);
            Optimization::SsaRewritePass(program);
            auto resources = Optimization::ResourceDiscoverPass(program, profile);
            Optimization::ResourcePatchingPass(info, resources, profile);
            IR::Inst* atomic{};
            const auto atomic_opcode = opcode == Gcn::Opcode::DS_APPEND
                                           ? IR::Opcode::BufferAtomicIAdd32
                                           : IR::Opcode::BufferAtomicISub32;
            for (auto& emitted : block.Instructions()) {
                if (emitted.GetOpcode() == atomic_opcode) {
                    atomic = &emitted;
                }
            }
            assert(atomic && !atomic->Arg(1).IsImmediate());
            // Evaluate a dispatch only after proving the module still reads user data at runtime.
            dynamic_base.Inst()->ReplaceUsesWith(ir.Imm32(snapshot));
            Optimization::ConstantPropagationPass(program.post_order_blocks);
            assert(atomic->Arg(1).IsImmediate());
            assert(atomic->Arg(1).U32() == ((snapshot >> 16) + 12) / 4);
        }
    }
    std::puts("GDS: 4 append/consume dispatch cases preserve dynamic M0");
}
