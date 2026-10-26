// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// port: from TdlifePeng/shadPS4 7db6e4ba5 and 1a3931981 (branch 1886), adapted to shadPS4 0.20.1.

#pragma once

#include <array>
#include <vector>
#include "common/types.h"

namespace Shader {
struct Info;
struct Profile;
struct RuntimeInfo;
} // namespace Shader

namespace Shader::Backend::SPIRV {

// Output location layout shared by the auxiliary geometry shader (output side) and the
// fragment shader input declaration (input side). Locations are assigned by a running counter:
// per-vertex attributes have contiguous param_index, so anchoring each expanded attribute onto
// [param_index, param_index + 2] would overlap.
struct PerVertexLocations {
    // Output location for each non-default input slot. For per-vertex attributes this is
    // the start of 3 consecutive locations; for others it is a single location.
    std::array<u32, 32> slot_loc{};
    // Location of the injected smooth barycentric-coordinate attribute (vec2, I/J).
    u32 bary_coord_loc = 0;
};

// Whether the per-vertex interpolation fallback applies to this fragment shader: neither AMD
// explicit vertex parameter nor KHR barycentric, a direct VS->FS triangle-list pipeline, no
// barycentric variant other than persp_center and no clip-distance emulation.
[[nodiscard]] bool CanPerVertexInterp(const Shader::Profile& profile,
                                      const Shader::RuntimeInfo& runtime_info);

// Bitmask of the barycentric variants the fallback cannot reproduce: bit0 persp_sample,
// bit1 persp_centroid, bit2 persp_pull_model, bit3 linear_sample, bit4 linear_center,
// bit5 linear_centroid.
[[nodiscard]] u32 UnsupportedBarycentricMask(const Shader::RuntimeInfo& runtime_info);

// Whether an auxiliary geometry shader must be generated for this fragment shader: the fallback
// applies and the shader reads a per-vertex attribute or the persp-center barycentric.
[[nodiscard]] bool NeedsPerVertexAuxGS(const Shader::Profile& profile, const Shader::Info& info,
                                       const Shader::RuntimeInfo& runtime_info);

// Output location layout for the fragment shader's non-default inputs: per-vertex attributes
// occupy 3 consecutive locations when the fallback applies, 1 otherwise.
[[nodiscard]] PerVertexLocations ComputePerVertexLocations(const Shader::Profile& profile,
                                                           const Shader::Info& info,
                                                           const Shader::RuntimeInfo& runtime_info);

// Emits the auxiliary geometry shader: per-vertex attributes become three flat outputs (P0, P10,
// P20 as the three vertices' values), a smooth vec2 runs (0,0)/(1,0)/(0,1) across the triangle so
// interpolation gives I/J, and the port's motion-vector varyings pass through.
[[nodiscard]] std::vector<u32> EmitPerVertexAuxGS(const Shader::Profile& profile,
                                                  const Shader::Info& info,
                                                  const Shader::RuntimeInfo& runtime_info);

} // namespace Shader::Backend::SPIRV
