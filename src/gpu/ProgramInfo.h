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

#include <optional>
#include <string>
#include <unordered_map>
#include "gpu/Program.h"
#include "gpu/Swizzle.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/ir/XferFacts.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/ColorWriteMask.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class Context;
struct DrawIR;
class GeometryProcessor;
class XferProcessor;

struct SamplerInfo {
  std::shared_ptr<Texture> texture;
  SamplerState state;
};

/**
 * One draw's program-lookup input and result: the pipeline state and the expression the draw asks
 * for (the carried DrawIR), plus the selection resolved from them by getProgram().
 */
class ProgramInfo {
 public:
  /**
   * The draw's program description: the expression rides the carried DrawIR (attached through
   * setCarriedIR before first use), so the matcher, the program key and the upload never read a
   * processor list. blendFormulaHasCoverage carries the one blend-formula fact that used to come
   * from the processor order: whether coverage processors follow the color ones.
   */
  ProgramInfo(RenderTarget* renderTarget, const GeometryLayout& geometryLayout,
              const std::optional<DstTextureInfo>& dstTexture, BlendMode blendMode,
              bool blendFormulaHasCoverage);

  /**
   * The JIT-route constructor: the processor tree the runtime ProgramBuilder walks. The AOT route
   * (the layout-and-carried-IR constructor above) and this route share the pipeline state and the
   * uniform plumbing below; only the program source differs (precompiled selection vs on-the-fly
   * emission).
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

  /// The output formula facts: whether the shader runs the xfer stage and where its destination
  /// comes from. Every reader below (writer, matcher, program key) addresses this value.
  const XferFacts& getXferFacts() const {
    return xferFacts;
  }

  BlendMode getBlendMode() const {
    return blendMode;
  }

  Swizzle getOutputSwizzle() const;

  /// The geometry value every reader addresses: the attribute layout, the physical slots and the
  /// uniform values, built by the draw itself.
  const GeometryLayout& getGeometryLayout() const {
    return geometryLayout;
  }

  const std::vector<Attribute>& getVertexAttributes() const;

  const std::vector<Attribute>& getInstanceAttributes() const;

  PipelineColorAttachment getPipelineColorAttachment() const;

  int getSampleCount() const;

  /**
   * Resolves the program for this info from the caches. Precompiled artifacts are the only
   * program source: a miss drops the draw, with no runtime fallback. The first call also runs the
   * draw's single selection and keeps the resulting plan on this info (see getPlan), so the upload
   * never re-matches.
   */
  std::shared_ptr<Program> getProgram();

  /**
   * Writes the pipeline-state fields the program keys carry — everything past the processor keys
   * (blend mode, output swizzle, cull, target format and samples, colour write mask, depth and
   * stencil). The plain route's key and the AOT route's key share this tail, so the two routes
   * encode identical state identically.
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

  /**
   * Returns the GLSL profile tag ("opengl" for desktop GL, "opengles" for the GLES-family
   * contexts: GLES, WebGL, SwiftShader) the precompiled bundle must serve this context from.
   */
  std::string glProfileTag() const;

  /**
   * Returns true when the loaded precompiled bundle was built for this context's GLSL profile;
   * a bundle for the other GL profile hashes its entries under a different tag and can never
   * serve this context.
   */
  bool glProfileMatchesCache() const;

  /**
   * Returns true when every texture sampled by this program uses the 2D texture type.
   * External (OES) textures are not supported by the precompiled shaders.
   */
  bool samplersAre2D() const;

 private:
  RenderTarget* renderTarget = nullptr;
  // The JIT route's processor tree; null on the AOT route.
  GeometryProcessor* geometryProcessor = nullptr;
  std::vector<FragmentProcessor*> fragmentProcessors = {};
  std::unordered_map<const Processor*, int> processorIndices = {};
  // This value is also the index in fragmentProcessors where coverage processors begin.
  size_t numColorProcessors = 0;
  XferProcessor* xferProcessor = nullptr;
  GeometryLayout geometryLayout = {};
  /// The xfer facts derived from the processor once per program: the shader-side blend flag and
  /// the destination it samples. The writers and the matcher read this, not the processor.
  XferFacts xferFacts = {};
  BlendMode blendMode = BlendMode::SrcOver;
  // Whether coverage processors follow the color ones — the only fragment-list fact the pipeline's
  // blend formula reads (see the AOT-route constructor for the list-free form).
  bool hasCoverageForBlend = false;
  CullMode cullMode = CullMode::None;
  DepthStencilDescriptor depthStencil = {};
  uint32_t colorWriteMask = ColorWriteMask::All;

  /// Derives the xfer facts from the processor once, at construction.
  void updateXferFacts(const std::optional<DstTextureInfo>& dstTexture);

  void updateProcessorIndices();

  std::vector<SamplerInfo> getSamplers() const;

  void updateUniformDataSuffix(UniformData* vertexUniformData, UniformData* fragmentUniformData,
                               const Processor* processor) const;

  // The diagnostics-only signature (see setEffectSignature).
  std::string effectSignature = {};

  // The draw's single selection result (see getPlan), filled by getProgram()'s first call. An
  // input to the upload, never recomputed; null when no selection ran.

  friend class ProgramWriter;
  friend class ChainProgramWriter;
};
}  // namespace tgfx
