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

#include <Metal/Metal.h>
#include <set>
#include <string>
#include <vector>
#include "tgfx/gpu/ShaderModule.h"
#include "MetalResource.h"

namespace tgfx {

class MetalGPU;

// Vertex buffers are bound at high Metal buffer indices (starting from this value and counting
// down), while uniform buffers produced by SPIRV-Cross occupy the low indices (0, 1, ...).
// This separation avoids index collisions in cross-compiled (GLSL -> SPIR-V -> MSL) pipelines,
// where uniform buffer bindings are auto-assigned from 0. The same strategy is used by MoltenVK
// and Google Dawn (WebGPU). Metal supports up to 31 buffer slots per shader stage.
static constexpr unsigned kVertexBufferIndexStart = 30;

/**
 * Metal shader module implementation with GLSL to MSL conversion. It compiles the GLSL source into
 * an MTLLibrary and retains the original GLSL code so that MetalRenderPipeline can re-compile with
 * sample mask injection when needed.
 */
class MetalShaderModule : public ShaderModule, public MetalResource {
 public:
  static std::shared_ptr<MetalShaderModule> Make(MetalGPU* gpu,
                                               const ShaderModuleDescriptor& descriptor);

  /**
   * Returns the Metal library containing the compiled shader.
   */
  id<MTLLibrary> metalLibrary() const {
    return library;
  }

  /**
   * Returns the shader stage (vertex or fragment) of this module.
   */
  ShaderStage stage() const {
    return _stage;
  }

  /**
   * Returns the original GLSL source code.
   */
  const std::string& glslCode() const {
    return _glslCode;
  }

 protected:
  void onRelease(MetalGPU* gpu) override;

 private:
  MetalShaderModule(MetalGPU* gpu, const ShaderModuleDescriptor& descriptor);
  ~MetalShaderModule() override = default;

  bool compileShader(id<MTLDevice> device, const std::string& glslCode, ShaderStage stage);
  std::string convertGLSLToMSL(const std::string& glslCode, ShaderStage stage);

  id<MTLLibrary> library = nil;
  ShaderStage _stage = ShaderStage::Vertex;
  std::string _glslCode;

  friend class MetalGPU;
};

struct SampleMaskCompileResult {
  id<MTLLibrary> library = nil;
  uint32_t constantID = 0;
};

// Re-compile a fragment shader GLSL with sample mask injection. The function first compiles the
// GLSL to SPIR-V to discover which constant_id values are already in use, picks an unused one,
// injects tgfx_SampleMask + gl_SampleMask output, and re-compiles the modified GLSL to produce
// a new MTLLibrary. Returns a nil library on failure.
SampleMaskCompileResult CompileFragmentShaderWithSampleMask(id<MTLDevice> device,
                                                            const std::string& glslCode);

}  // namespace tgfx