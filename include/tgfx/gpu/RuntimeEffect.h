/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/core/Image.h"
#include "tgfx/core/MapDirection.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

/**
 * RuntimeEffect supports creating custom ImageFilter objects whose shaders are supplied by the
 * caller and built inside onDraw() through the raw GPU API (GPU::createShaderModule +
 * GPU::createRenderPipeline + CommandEncoder). tgfx does not translate shader code between
 * formats at runtime, so the shader must be written in the native form of the backend that will
 * execute it (ShaderCodeFormat):
 *   - OpenGL: GLSL text ("#version 300 es", or "#version 150" on desktop OpenGL).
 *   - Metal:  MSL text with entry functions named `main0`, one source per stage, or a metallib.
 *             Clip-space positions follow the Metal convention (y up, after the origin transform
 *             the render target applies), so mirror the y sign the GLSL path produces when porting
 *             an effect from GLSL.
 *   - Vulkan: SPIR-V binary translated offline, for example with `glslc --target-env=vulkan1.1`.
 *             The Vulkan driver cannot compile text.
 *   - WebGPU: WGSL text.
 *   - D3D12:  HLSL text with the entry function named `main`, or DXBC bytecode. A cbuffer binds
 *             any register(bN) as long as uniformSlots reports that N under its name; texture
 *             samplers use (t{K}, s{K}) with K the index in layout.textureSamplers, and vertex
 *             inputs use the TEXCOORD{N} semantic with N the attribute's global index.
 * For every non-GLSL format the descriptor must fill ShaderModuleDescriptor::uniformSlots, and
 * the entries must match RenderPipelineDescriptor::layout.uniformBlocks. Cross-stage varying
 * interfaces are validated by this library for GLSL text only; with the native formats the
 * compiler and the backend validate the interface (a mismatch fails pipeline creation).
 */
class RuntimeEffect {
 public:
  /**
   * Constructs a RuntimeEffect with the given extra input images.
   * @param extraInputs A collection of additional input images used during rendering. When the
   * onDraw() method is called, these extraInputs will be converted to inputTextures.
   * inputTextures[0] represents the source image for the ImageFilter, and extraInputs correspond to
   * inputTextures[1...n] in order.
   */
  explicit RuntimeEffect(const std::vector<std::shared_ptr<Image>>& extraInputs = {})
      : extraInputs(extraInputs) {
  }

  virtual ~RuntimeEffect() = default;

  /**
   * Returns the bounds of the image that will be produced by this filter when it is applied to an
   * image of the given bounds. MapDirection::Forward is used to determine which pixels of the
   * destination canvas a source image rect would touch after filtering. MapDirection::Reverse
   * is used to determine which rect of the source image would be required to fill the given
   * rect (typically, clip bounds).
   */
  virtual Rect filterBounds(const Rect& srcRect, MapDirection) const {
    return srcRect;
  }

 protected:
  /**
   * Applies the effect to the input textures and draws the result to the specified output texture.
   * inputTextures[0] represents the source image for the ImageFilter, and extraInputs correspond to
   * inputTextures[1...n] in order.
   */
  virtual bool onDraw(CommandEncoder* encoder,
                      const std::vector<std::shared_ptr<Texture>>& inputTextures,
                      std::shared_ptr<Texture> outputTexture, const Point& offset) const = 0;

 private:
  std::vector<std::shared_ptr<Image>> extraInputs = {};

  friend class RuntimeProgramCreator;
  friend class RuntimeDrawTask;
  friend class RuntimeImageFilter;
};
}  // namespace tgfx
