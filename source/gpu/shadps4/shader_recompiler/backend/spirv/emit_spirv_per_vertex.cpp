// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// port: from TdlifePeng/shadPS4 7db6e4ba5 (branch 1886), adapted to shadPS4 0.20.1; the port's
// motion-vector varyings pass through.
//
// GCN pixel shaders can read the three per-vertex values of an attribute (V_INTERP_MOV_F32 with
// P0/P10/P20) and interpolate them themselves with the barycentric registers I/J. The Order unpacks
// f16-packed attributes that way: it marks them flat (the packed bits cannot be interpolated) and
// rebuilds the interpolated value. Without VK_AMD_shader_explicit_vertex_parameter or
// VK_KHR_fragment_shader_barycentric (GTX 10, Intel HD and Arc) both inputs were lost: the
// characters drew black (issues #1, #10, #15). This geometry shader gives them back.

#include <sirit/sirit.h>
#include "shader_recompiler/backend/spirv/emit_spirv_per_vertex.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/runtime_info.h"

namespace Shader::Backend::SPIRV {

using Sirit::Id;

namespace {
constexpr u32 SpirvVersion1_5 = 0x00010500;

bool HasUnsupportedBarycentricVariant(const RuntimeInfo& runtime_info) {
    return UnsupportedBarycentricMask(runtime_info) != 0;
}
} // namespace

u32 UnsupportedBarycentricMask(const RuntimeInfo& runtime_info) {
    const auto& af = runtime_info.hw.fs.addr_flags;
    u32 mask = 0;
    mask |= af.persp_sample_ena ? 1u << 0 : 0u;
    mask |= af.persp_centroid_ena ? 1u << 1 : 0u;
    mask |= af.persp_pull_model_ena ? 1u << 2 : 0u;
    mask |= af.linear_sample_ena ? 1u << 3 : 0u;
    mask |= af.linear_center_ena ? 1u << 4 : 0u;
    mask |= af.linear_centroid_ena ? 1u << 5 : 0u;
    return mask;
}

bool CanPerVertexInterp(const Profile& profile, const RuntimeInfo& runtime_info) {
    const auto& fs = runtime_info.hw.fs;
    // Clip-distance emulation takes input location 0, which the geometry shader does not pass.
    return fs.vs_direct_triangles && !profile.supports_amd_shader_explicit_vertex_parameter &&
           !profile.supports_fragment_shader_barycentric &&
           !HasUnsupportedBarycentricVariant(runtime_info) && !fs.clip_distance_emulation;
}

bool NeedsPerVertexAuxGS(const Profile& profile, const Info& info, const RuntimeInfo& runtime_info) {
    if (!CanPerVertexInterp(profile, runtime_info)) {
        return false;
    }
    const auto& fs = runtime_info.hw.fs;
    if (fs.addr_flags.persp_center_ena) {
        return true;
    }
    for (u32 i = 0; i < fs.num_inputs; i++) {
        if (info.fs_interpolation[i].primary == Qualifier::PerVertex) {
            return true;
        }
    }
    return false;
}

PerVertexLocations ComputePerVertexLocations(const Profile& profile, const Info& info,
                                             const RuntimeInfo& runtime_info) {
    const auto& fs = runtime_info.hw.fs;
    const bool can_expand = CanPerVertexInterp(profile, runtime_info);
    PerVertexLocations locs;
    const bool has_clip = fs.clip_distance_emulation;
    const u32 num_inputs = fs.num_inputs - (has_clip ? 1 : 0);
    u32 loc = has_clip ? 1 : 0;
    for (u32 i = 0; i < num_inputs; i++) {
        if (fs.inputs[i].IsDefault()) {
            continue;
        }
        locs.slot_loc[i] = loc;
        const bool per_vertex = info.fs_interpolation[i].primary == Qualifier::PerVertex;
        loc += (per_vertex && can_expand) ? 3 : 1;
    }
    locs.bary_coord_loc = loc;
    return locs;
}

namespace {

class PerVertexAuxGSEmitter : public Sirit::Module {
public:
    PerVertexAuxGSEmitter(const Profile& profile, const Info& info_,
                          const RuntimeInfo& runtime_info)
        : Sirit::Module{SpirvVersion1_5}, info{info_}, fs{runtime_info.hw.fs} {
        void_id = TypeVoid();
        float_id = TypeFloat(32);
        uint_id = TypeUInt(32U);
        int_id = TypeInt(32U, true);
        vec2_id = TypeVector(float_id, 2);
        vec4_id = TypeVector(float_id, 4);

        const Id float_arr{TypeArray(float_id, Constant(uint_id, 1U))};
        gl_per_vertex_type = TypeStruct(vec4_id, float_id, float_arr, float_arr);
        Decorate(gl_per_vertex_type, spv::Decoration::Block);
        MemberDecorate(gl_per_vertex_type, 0U, spv::Decoration::BuiltIn,
                       static_cast<u32>(spv::BuiltIn::Position));
        MemberDecorate(gl_per_vertex_type, 1U, spv::Decoration::BuiltIn,
                       static_cast<u32>(spv::BuiltIn::PointSize));
        MemberDecorate(gl_per_vertex_type, 2U, spv::Decoration::BuiltIn,
                       static_cast<u32>(spv::BuiltIn::ClipDistance));
        MemberDecorate(gl_per_vertex_type, 3U, spv::Decoration::BuiltIn,
                       static_cast<u32>(spv::BuiltIn::CullDistance));

        num_inputs = fs.num_inputs - (fs.clip_distance_emulation ? 1 : 0);
        locs = ComputePerVertexLocations(profile, info_, runtime_info);
        for (u32 i = 0; i < num_inputs; i++) {
            if (!fs.inputs[i].IsDefault()) {
                input_slots.push_back(i);
                per_vertex[i] = info.fs_interpolation[i].primary == Qualifier::PerVertex;
            }
        }
        has_bary_coord = fs.addr_flags.persp_center_ena != 0;
    }

    void Emit() {
        AddCapability(spv::Capability::Shader);
        AddCapability(spv::Capability::Geometry);
        const Id void_function{TypeFunction(void_id)};
        main = OpFunction(void_id, spv::FunctionControlMask::MaskNone, void_function);
        AddExecutionMode(main, spv::ExecutionMode::Triangles);
        AddExecutionMode(main, spv::ExecutionMode::OutputTriangleStrip);
        AddExecutionMode(main, spv::ExecutionMode::OutputVertices, 3U);
        AddExecutionMode(main, spv::ExecutionMode::Invocations, 1U);
        DefineInputs();
        DefineOutputs();
        AddEntryPoint(spv::ExecutionModel::Geometry, main, "main", interfaces);
        AddLabel(OpLabel());
        EmitMain();
    }

private:
    Id Int(s32 value) {
        return Constant(int_id, value);
    }

    Id AddInput(Id type, u32 location) {
        const Id input{AddGlobalVariable(TypePointer(spv::StorageClass::Input, type),
                                         spv::StorageClass::Input)};
        Decorate(input, spv::Decoration::Location, location);
        interfaces.push_back(input);
        return input;
    }

    Id AddOutput(Id type, u32 location) {
        const Id output{AddGlobalVariable(TypePointer(spv::StorageClass::Output, type),
                                          spv::StorageClass::Output)};
        Decorate(output, spv::Decoration::Location, location);
        interfaces.push_back(output);
        return output;
    }

    void DefineInputs() {
        gl_in = AddGlobalVariable(
            TypePointer(spv::StorageClass::Input,
                        TypeArray(gl_per_vertex_type, Constant(uint_id, 3U))),
            spv::StorageClass::Input);
        interfaces.push_back(gl_in);
        // Vertex shader outputs sit at param_index (+1 behind an emulated clip distance).
        const Id input_arr{TypeArray(vec4_id, Constant(uint_id, 3U))};
        for (const u32 i : input_slots) {
            input_ids.push_back(
                AddInput(input_arr, fs.inputs[i].param_index + (fs.clip_distance_emulation ? 1 : 0)));
        }
        if (fs.motion_vectors) {
            motion_in[0] = AddInput(input_arr, MotionVectors::CurrentLocation);
            motion_in[1] = AddInput(input_arr, MotionVectors::PreviousLocation);
        }
    }

    void DefineOutputs() {
        gl_out = AddGlobalVariable(TypePointer(spv::StorageClass::Output, gl_per_vertex_type),
                                   spv::StorageClass::Output);
        interfaces.push_back(gl_out);
        output_ids.resize(input_slots.size());
        pv_output_ids.resize(input_slots.size());
        for (size_t k = 0; k < input_slots.size(); k++) {
            const u32 i = input_slots[k];
            const u32 loc = locs.slot_loc[i];
            if (per_vertex[i]) {
                // P0/P10/P20 as three flat outputs: never interpolate packed bit patterns.
                for (u32 j = 0; j < 3; j++) {
                    pv_output_ids[k][j] = AddOutput(vec4_id, loc + j);
                    Decorate(pv_output_ids[k][j], spv::Decoration::Flat);
                }
            } else {
                // Passed through; the fragment shader input decides the interpolation.
                output_ids[k] = AddOutput(vec4_id, loc);
            }
        }
        if (has_bary_coord) {
            bary_coord = AddOutput(vec2_id, locs.bary_coord_loc);
        }
        if (fs.motion_vectors) {
            motion_out[0] = AddOutput(vec4_id, MotionVectors::CurrentLocation);
            motion_out[1] = AddOutput(vec4_id, MotionVectors::PreviousLocation);
        }
    }

    void EmitMain() {
        const Id input_vec4{TypePointer(spv::StorageClass::Input, vec4_id)};
        const Id output_vec4{TypePointer(spv::StorageClass::Output, vec4_id)};
        std::array<Id, 3> pos;
        for (int v = 0; v < 3; v++) {
            pos[v] = OpLoad(vec4_id, OpAccessChain(input_vec4, gl_in, Int(v), Int(0)));
        }
        std::vector<std::array<Id, 3>> attr_vals(input_slots.size());
        for (size_t k = 0; k < input_slots.size(); k++) {
            for (int v = 0; v < 3; v++) {
                attr_vals[k][v] = OpLoad(vec4_id, OpAccessChain(input_vec4, input_ids[k], Int(v)));
            }
        }
        std::array<std::array<Id, 3>, 2> motion_vals{};
        if (fs.motion_vectors) {
            for (int m = 0; m < 2; m++) {
                for (int v = 0; v < 3; v++) {
                    motion_vals[m][v] =
                        OpLoad(vec4_id, OpAccessChain(input_vec4, motion_in[m], Int(v)));
                }
            }
        }
        // Barycentrics: vertex 0 -> (0,0), 1 -> (1,0), 2 -> (0,1), so I and J weigh P10 and P20.
        std::array<Id, 3> bary_value;
        if (has_bary_coord) {
            const Id zero{Constant(float_id, 0.0f)};
            const Id one{Constant(float_id, 1.0f)};
            bary_value[0] = ConstantComposite(vec2_id, zero, zero);
            bary_value[1] = ConstantComposite(vec2_id, one, zero);
            bary_value[2] = ConstantComposite(vec2_id, zero, one);
        }
        for (int v = 0; v < 3; v++) {
            OpStore(OpAccessChain(output_vec4, gl_out, Int(0)), pos[v]);
            for (size_t k = 0; k < input_slots.size(); k++) {
                if (per_vertex[input_slots[k]]) {
                    for (int j = 0; j < 3; j++) {
                        OpStore(pv_output_ids[k][j], attr_vals[k][j]);
                    }
                } else {
                    OpStore(output_ids[k], attr_vals[k][v]);
                }
            }
            if (has_bary_coord) {
                OpStore(bary_coord, bary_value[v]);
            }
            if (fs.motion_vectors) {
                OpStore(motion_out[0], motion_vals[0][v]);
                OpStore(motion_out[1], motion_vals[1][v]);
            }
            OpEmitVertex();
        }
        OpEndPrimitive();
        OpReturn();
        OpFunctionEnd();
    }

    const Info& info;
    const HwFragmentRuntimeInfo& fs;
    PerVertexLocations locs;
    u32 num_inputs{};
    bool has_bary_coord{};
    std::vector<u32> input_slots;
    std::array<bool, 32> per_vertex{};
    std::vector<Id> input_ids;
    std::vector<Id> output_ids;
    std::vector<std::array<Id, 3>> pv_output_ids;
    std::array<Id, 2> motion_in{};
    std::array<Id, 2> motion_out{};

    Id main;
    Id void_id;
    Id float_id;
    Id uint_id;
    Id int_id;
    Id vec2_id;
    Id vec4_id;
    Id gl_per_vertex_type;
    Id gl_in;
    Id gl_out;
    Id bary_coord;
    std::vector<Id> interfaces;
};

} // namespace

std::vector<u32> EmitPerVertexAuxGS(const Profile& profile, const Info& info,
                                    const RuntimeInfo& runtime_info) {
    PerVertexAuxGSEmitter ctx{profile, info, runtime_info};
    ctx.Emit();
    return ctx.Assemble();
}

} // namespace Shader::Backend::SPIRV
