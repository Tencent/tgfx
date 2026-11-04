/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

/**
 * Describes the payload carried by a ShaderModuleDescriptor. Each value names what the payload
 * actually is; the backend accepts only the formats its driver can compile or load natively and
 * rejects everything else with an error. tgfx never translates between formats at runtime, so a
 * custom shader must be provided in the native form of the backend that will execute it:
 *   - OpenGL: GLSL text   - Metal: MSL text or METALLIB - Vulkan: SPIRV
 *   - WebGPU: WGSL text   - D3D12: HLSL text or DXBC
 */
enum class ShaderCodeFormat {
  /**
   * GLSL text source in `code`, compiled by the OpenGL driver. Only the OpenGL backend accepts it.
   */
  GLSL,
  /**
   * Pre-compiled SPIR-V binary in `binaryData`, loaded directly by the Vulkan backend. The driver
   * cannot compile text, so GLSL must be translated offline (for example with
   * `glslc --target-env=vulkan1.1`).
   */
  SPIRV,
  /**
   * Metal Shading Language text source in `code`, compiled by the Metal driver at runtime. The
   * entry functions must be named `main0`, and each module carries one stage, so vertex and
   * fragment sources are supplied as separate descriptors.
   */
  MSL,
  /**
   * WebGPU Shading Language text source in `code`, compiled by the browser.
   */
  WGSL,
  /**
   * Pre-compiled Metal library (metallib) binary in `binaryData`, loaded without compilation.
   */
  METALLIB,
  /**
   * HLSL text source in `code`, compiled at runtime by the system D3DCompiler (`D3DCompile`). The
   * entry function must be named `main`. A cbuffer may bind any `register(bN)` it likes as long
   * as uniformSlots reports that N under the block name. Texture samplers must use `(t{K}, s{K})`
   * where K is the entry's index in RenderPipelineDescriptor::layout.textureSamplers, and vertex
   * inputs must use the `TEXCOORD{N}` semantic where N is the attribute's global index across the
   * vertex buffer layouts.
   */
  HLSL,
  /**
   * Pre-compiled DXBC bytecode in `binaryData`, shipped to the pipeline without compilation.
   */
  DXBC,
};

/**
 * ShaderModuleDescriptor describes the properties required to create a ShaderModule.
 */
class ShaderModuleDescriptor {
 public:
  /**
   * The shader text source for the text formats (GLSL, MSL, WGSL, HLSL). Ignored by the binary
   * formats (SPIRV, METALLIB, DXBC), which read binaryData instead.
   */
  std::string code;

  /**
   * Pre-compiled shader binary for the binary formats (SPIRV, METALLIB, DXBC). Ignored by the
   * text formats.
   */
  std::vector<uint8_t> binaryData;

  /**
   * Uniform-block name to physical binding slot. The GLSL path collects this map automatically by
   * scanning the source, so callers may leave it empty there. Every other format must fill it in:
   * the binary artifacts and native text formats carry no parseable GLSL, and pipeline creation
   * rejects a uniform block that the module cannot resolve by name.
   * @param key    The uniform block name declared by the shader. The name must match the entry in
   *               RenderPipelineDescriptor::layout.uniformBlocks that feeds this block.
   * @param value  The physical slot the shader binds the block to: the descriptor set/binding for
   *               SPIRV, the `[[buffer(N)]]` index for MSL/METALLIB, the `@binding(N)` index for
   *               WGSL, or the `b{N}` register for HLSL/DXBC.
   */
  std::map<std::string, unsigned> uniformSlots;

  /**
   * The payload format. Defaults to GLSL text source, which only the OpenGL backend accepts.
   */
  ShaderCodeFormat format = ShaderCodeFormat::GLSL;

  /**
   * The shader stage. The OpenGL backend needs it to pick the shader type, and every backend uses
   * it for stage-scoped validation and diagnostics.
   */
  ShaderStage stage = ShaderStage::Vertex;
};

/**
 * ShaderModule is an internal object that serves as a container for shader code，allowing it to
 * be submitted to the GPU for execution within a pipeline.
 */
class ShaderModule {
 public:
  virtual ~ShaderModule() = default;
};
}  // namespace tgfx
