/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <string>
#include <unordered_map>
#include "gpu/Program.h"
#include "gpu/Swizzle.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/ColorWriteMask.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class Context;
class GeometryProcessor;
class XferProcessor;

struct SamplerInfo {
  std::shared_ptr<Texture> texture;
  SamplerState state;
};

/**
 * One draw's program-lookup input and result: the processor tree, the pipeline state, and the
 * program the cache resolves from them.
 */
class ProgramInfo {
 public:
  /**
   * The processor tree the runtime ProgramBuilder walks: the geometry processor, the fragment
   * chain and the xfer processor.
   */
  ProgramInfo(RenderTarget* renderTarget, GeometryProcessor* geometryProcessor,
              std::vector<FragmentProcessor*> fragmentProcessors, size_t numColorProcessors,
              XferProcessor* xferProcessor, BlendMode blendMode);

  size_t numColorFragmentProcessors() const {
    return numColorProcessors;
  }

  size_t numFragmentProcessors() const {
    return fragmentProcessors.size();
  }

  const GeometryProcessor* getGeometryProcessor() const {
    return geometryProcessor;
  }

  const FragmentProcessor* getFragmentProcessor(size_t idx) const {
    return fragmentProcessors[idx];
  }

  const XferProcessor* getXferProcessor() const;

  /// The draw's render target: the engine-level writers (RTAdjust) read its size and origin.
  RenderTarget* getRenderTarget() const {
    return renderTarget;
  }

  BlendMode getBlendMode() const {
    return blendMode;
  }

  Swizzle getOutputSwizzle() const;

  const std::vector<Attribute>& getVertexAttributes() const;

  const std::vector<Attribute>& getInstanceAttributes() const;

  PipelineColorAttachment getPipelineColorAttachment() const;

  int getSampleCount() const;

  /**
   * Resolves the program for this info from the caches: the processor keys plus the pipeline
   * state name the entry, and a miss emits the program through the runtime ProgramBuilder.
   */
  std::shared_ptr<Program> getProgram();

  /**
   * Writes the pipeline-state fields the program keys carry — everything past the processor keys
   * (blend mode, output swizzle, cull, target format and samples, colour write mask, depth and
   * stencil).
   */
  void writePipelineStateKey(BytesKey* key) const;

  /**
   * Whether coverage processors follow the color ones. The pipeline's blend formula reads it, so
   * the program key carries it: two draws that differ only here need different pipelines.
   */
  bool blendFormulaHasCoverage() const {
    return hasCoverageForBlend;
  }

  std::shared_ptr<GPUBuffer> getUniformBuffer(const Program* program, size_t* vertexOffset,
                                              size_t* fragmentOffset) const;

  void bindUniformBufferAndUnloadToGPU(const Program* program,
                                       std::shared_ptr<GPUBuffer> uniformBuffer,
                                       RenderPass* renderPass, size_t vertexOffset,
                                       size_t fragmentOffset) const;

  /**
   * Sets the uniform data and texture samplers on the render pass for the given program.
   */
  /**
   * Uploads every uniform and binds the samplers of the program. Returns false when the program
   * needs a uniform block but no backing store is available (allocation/map failure): the caller
   * must skip the draw rather than submit it with unwritten uniforms.
   */
  bool setUniformsAndSamplers(RenderPass* renderPass, Program* program) const;

  /**
   * Returns the cull face mode used for rendering.
   */
  CullMode getCullMode() const {
    return cullMode;
  }

  /**
   * Sets the cull face mode used for rendering.
   */
  void setCullMode(CullMode mode) {
    cullMode = mode;
  }

  /**
   * Returns the depth/stencil descriptor that will be applied to the render pipeline. The default
   * value is a no-op (no depth test, no stencil test) so existing draw ops which never touch the
   * stencil buffer keep their previous behaviour.
   */
  const DepthStencilDescriptor& getDepthStencil() const {
    return depthStencil;
  }

  /**
   * Sets the depth/stencil descriptor that will be applied to the render pipeline so the program
   * cache key reflects the requested stencil state.
   */
  void setDepthStencil(const DepthStencilDescriptor& descriptor) {
    depthStencil = descriptor;
  }

  /**
   * Returns the colour write mask applied to the colour attachment when constructing the render
   * pipeline. Defaults to ColorWriteMask::All, meaning every channel is written.
   */
  uint32_t getColorWriteMask() const {
    return colorWriteMask;
  }

  /**
   * Sets the colour write mask applied to the colour attachment. Pass 0 to disable colour writes
   * entirely (typical for a stencil-only mask pass); pass any combination of ColorWriteMask flags
   * to restrict writes to selected channels. Must be called before getProgram() so the program
   * cache distinguishes pipelines that share shaders but differ only in colour write mask.
   */
  void setColorWriteMask(uint32_t mask) {
    colorWriteMask = mask;
  }

  /**
   * Returns the index of the processor in the ProgramInfo. Returns -1 if the processor is not in
   * the ProgramInfo.
   */
  int getProcessorIndex(const Processor* processor) const;

  std::string getMangledSuffix(const Processor* processor) const;

  /** Formats a program key as the hex string the diagnostic records carry. */
  static std::string FormatDiagnosticKey(const BytesKey& key);

  /**
   * Returns the GPU backend used by this program's render target.
   */
  Backend backend() const;

  /**
   * The diagnostics-only effect signature, built by the op from its processor lists (empty when
   * diagnostic recording is off). Nothing that selects, keys or writes a program reads it.
   */
  void setEffectSignature(std::string signature) {
    effectSignature = std::move(signature);
  }

  const std::string& getEffectSignature() const {
    return effectSignature;
  }

  /**
   * Returns the context owning this program's render target, or null when the target is absent.
   */
  Context* getContext() const {
    return renderTarget != nullptr ? renderTarget->getContext() : nullptr;
  }

 private:
  RenderTarget* renderTarget = nullptr;
  GeometryProcessor* geometryProcessor = nullptr;
  std::vector<FragmentProcessor*> fragmentProcessors = {};
  std::unordered_map<const Processor*, int> processorIndices = {};
  // This value is also the index in fragmentProcessors where coverage processors begin.
  size_t numColorProcessors = 0;
  XferProcessor* xferProcessor = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  // Whether coverage processors follow the color ones: the only fragment-list fact the pipeline's
  // blend formula reads.
  bool hasCoverageForBlend = false;
  CullMode cullMode = CullMode::None;
  DepthStencilDescriptor depthStencil = {};
  uint32_t colorWriteMask = ColorWriteMask::All;

  void updateProcessorIndices();

  std::vector<SamplerInfo> getSamplers() const;

  void updateUniformDataSuffix(UniformData* vertexUniformData, UniformData* fragmentUniformData,
                               const Processor* processor) const;

  // The diagnostics-only signature (see setEffectSignature).
  std::string effectSignature = {};
};
}  // namespace tgfx
