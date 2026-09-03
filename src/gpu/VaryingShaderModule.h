/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "tgfx/gpu/RenderPipeline.h"
#include "tgfx/gpu/ShaderModule.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

/**
 * Maps one public logical uniform-block binding to the physical shader-stage slots that consume
 * it. A block declared in both stages (a shared, same-named uniform block, matching the OpenGL
 * program-level binding contract) populates both slots; a stage-exclusive block populates only
 * one. The physical slot value is backend-specific (Vulkan descriptor binding, Metal buffer
 * index, WebGPU binding, or D3D12 CBV register).
 */
struct UniformSlotMapping {
  std::optional<unsigned> vertexSlot;
  std::optional<unsigned> fragmentSlot;
};

/**
 * Internal base class for shader modules that declare a varying interface (vertex `out` or
 * fragment `in` declarations) and a set of uniform blocks. Both are only consumed by the backend
 * render pipelines for cross-stage validation and uniform-slot resolution, so they live in this
 * src/-side base class instead of the public ShaderModule API.
 */
class VaryingShaderModule : public ShaderModule {
 public:
  const std::unordered_map<std::string, int>& varyingDecls() const {
    return _varyingDecls;
  }

  /**
   * Returns the uniform blocks declared by this stage, keyed by block name and mapped to the
   * physical shader-stage slot the backend assigned to it. Ordered by name so slot resolution is
   * deterministic across runs.
   */
  const std::map<std::string, unsigned>& uniformSlots() const {
    return _uniformSlots;
  }

 protected:
  VaryingShaderModule(std::unordered_map<std::string, int> varyingDecls,
                      std::map<std::string, unsigned> uniformSlots)
      : _varyingDecls(std::move(varyingDecls)), _uniformSlots(std::move(uniformSlots)) {
  }

  /**
   * Sets the uniform-slot map after construction. Used by backends (e.g. D3D12) whose physical
   * slot assignment is only known partway through the constructor body, after the base class has
   * already been initialized.
   */
  void setUniformSlots(std::map<std::string, unsigned> uniformSlots) {
    _uniformSlots = std::move(uniformSlots);
  }

 private:
  std::unordered_map<std::string, int> _varyingDecls = {};
  std::map<std::string, unsigned> _uniformSlots = {};
};

/**
 * Resolves the public logical uniform-block bindings for a pipeline into per-stage physical slots,
 * shared by every SPIR-V based backend (Vulkan / Metal / D3D12 / WebGPU).
 *
 * Every uniform block declared in either shader stage must appear in `uniformBlocks` with a
 * visibility that covers that stage, and every entry in `uniformBlocks` must correspond to a
 * uniform block declared in each stage it targets. Missing declarations in either direction are
 * hard errors: `errorOut` receives a description and the function returns false so the caller can
 * reject pipeline creation instead of producing a pipeline with a dangling binding.
 *
 * A same-named block declared in both stages shares one `BindingEntry` (visibility ==
 * VertexFragment) and one logical binding, so a single RenderPass::setUniformBuffer() call feeds
 * both stages. The two declarations must be verbatim identical — same member list, member order,
 * member types, and precision qualifiers. This is not a tgfx-specific rule; GLSL/SPIR-V both
 * require it, and OpenGL's `glLinkProgram` is meant to reject any mismatch. In practice detection
 * is uneven across the backends and drivers this function serves:
 *   - Strict OpenGL implementations (e.g. SwiftShader ES) report the mismatch as a link error.
 *   - Many production GL drivers (WebGL, common Android / OpenHarmony vendor drivers) tolerate
 *     the milder cases such as a precision mismatch and link successfully.
 *   - The SPIR-V backends (Vulkan / Metal / D3D12 / WebGPU) compile each module independently,
 *     so nothing at either compile or pipeline creation time observes the other stage's member
 *     list; precision, member order, or member type differences all pass silently. When the two
 *     declarations diverge structurally, the same buffer bytes get reinterpreted through two
 *     different layouts and the render output is silently wrong.
 * tgfx cannot close this gap at pipeline creation time — module objects only carry the block name
 * and its physical slot, not the member list — so callers must guarantee the verbatim-identical
 * declaration themselves.
 *
 * `vertexModule` / `fragmentModule` may be null when the pipeline omits that stage.
 */
inline bool ResolveUniformSlots(const VaryingShaderModule* vertexModule,
                                const VaryingShaderModule* fragmentModule,
                                const std::vector<BindingEntry>& uniformBlocks,
                                std::map<unsigned, UniformSlotMapping>& mappingOut,
                                std::string& errorOut) {
  mappingOut.clear();
  std::unordered_set<std::string> vertexInLayout;
  std::unordered_set<std::string> fragmentInLayout;
  for (const auto& entry : uniformBlocks) {
    UniformSlotMapping mapping;
    if ((entry.visibility & ShaderVisibility::Vertex) && vertexModule != nullptr) {
      auto it = vertexModule->uniformSlots().find(entry.name);
      if (it == vertexModule->uniformSlots().end()) {
        errorOut = "vertex uniform block '" + entry.name + "' was not found";
        return false;
      }
      mapping.vertexSlot = it->second;
      vertexInLayout.insert(entry.name);
    }
    if ((entry.visibility & ShaderVisibility::Fragment) && fragmentModule != nullptr) {
      auto it = fragmentModule->uniformSlots().find(entry.name);
      if (it == fragmentModule->uniformSlots().end()) {
        errorOut = "fragment uniform block '" + entry.name + "' was not found";
        return false;
      }
      mapping.fragmentSlot = it->second;
      fragmentInLayout.insert(entry.name);
    }
    mappingOut[entry.binding] = mapping;
  }
  // Reverse check: every uniform block declared in either shader stage must be covered by
  // `uniformBlocks`. Missing this in the SPIR-V backends silently misroutes buffers at draw time
  // (WebGPU catches it at pipeline creation, Vulkan/D3D12 only through validation layers, Metal
  // defers until the draw, GL reads undefined data), so we surface it uniformly here — mirroring
  // the varying interface check that keeps vertex/fragment declarations in sync.
  if (vertexModule != nullptr) {
    for (const auto& [name, slot] : vertexModule->uniformSlots()) {
      (void)slot;
      if (vertexInLayout.find(name) == vertexInLayout.end()) {
        errorOut = "vertex uniform block '" + name +
                   "' is declared in the shader but missing from the pipeline layout";
        return false;
      }
    }
  }
  if (fragmentModule != nullptr) {
    for (const auto& [name, slot] : fragmentModule->uniformSlots()) {
      (void)slot;
      if (fragmentInLayout.find(name) == fragmentInLayout.end()) {
        errorOut = "fragment uniform block '" + name +
                   "' is declared in the shader but missing from the pipeline layout";
        return false;
      }
    }
  }
  return true;
}

}  // namespace tgfx
