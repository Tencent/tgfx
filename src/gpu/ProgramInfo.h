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
#include "gpu/PermutationMatch.h"
#include "gpu/Program.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/GeometryProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/ColorWriteMask.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class Context;

struct SamplerInfo {
  std::shared_ptr<Texture> texture;
  SamplerState state;
};

/**
 * Controls whether a program lookup may fall back to runtime shader generation.
 */
enum class ProgramLookupMode {
  /// Falls back to ProgramBuilder only when no precompiled bundle is loaded.
  AllowRuntimeFallback,
  /// Returns only programs whose provenance is ProgramOrigin::PrecompiledArtifact.
  PrecompiledOnly,
};

/**
 * This immutable object contains information needed to build a shader program and set API state for
 * a draw.
 */
class ProgramInfo {
 public:
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

  Swizzle getOutputSwizzle() const;

  const std::vector<Attribute>& getVertexAttributes() const {
    return geometryProcessor->vertexAttributes();
  }

  const std::vector<Attribute>& getInstanceAttributes() const {
    return geometryProcessor->instanceAttributes();
  }

  PipelineColorAttachment getPipelineColorAttachment() const;

  int getSampleCount() const;

  /**
   * Returns the index of the processor in the ProgramInfo. Returns -1 if the processor is not in
   * the ProgramInfo.
   */
  int getProcessorIndex(const Processor* processor) const;

  std::string getMangledSuffix(const Processor* processor) const;

  std::shared_ptr<Program> getProgram(
      ProgramLookupMode mode = ProgramLookupMode::AllowRuntimeFallback) const;

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
    cachedProgramKey.reset();
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
   * Sets the depth/stencil descriptor that will be applied to the render pipeline. Draw ops that
   * need stencil testing or writing call this before getProgram() so the pipeline descriptor
   * forwarded by GLSLProgramBuilder picks up the configuration and the program-cache key reflects
   * the requested stencil state.
   */
  void setDepthStencil(const DepthStencilDescriptor& descriptor) {
    depthStencil = descriptor;
    cachedProgramKey.reset();
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
    cachedProgramKey.reset();
  }

  /**
   * The program-cache key, computed once per info and shared by getProgram and the prepare-time
   * route checks. The setters above invalidate it, so it always reflects the current pipeline
   * state.
   */
  const BytesKey& programKey() const;

  // Returns the exact cache key for diagnostics without letting reporting code reconstruct it from
  // effect/pipeline signatures (which omit processor-key details).
  BytesKey programKeyForDiagnostics() const;

  /** Formats a program key as the hex string the diagnostic records carry. */
  static std::string FormatDiagnosticKey(const BytesKey& key);

  /**
   * Returns the GPU backend used by this program's render target.
   */
  Backend backend() const;

  /**
   * The whole-color-expression processor a re-expressing family (the chain interpreter or the
   * perlin kernel) built while matching this info, kept so the per-draw value write reuses it
   * instead of rebuilding it. The match walk stops at the first family that accepts the draw, so
   * the slot always holds the winner's expression; a family whose acceptance checks fail after
   * building may leave its (unused) expression behind, which only the winning family's writer
   * reads. Mutable like the match cache: an idempotence detail, not program state.
   */
  void setCachedReexpression(PlacementPtr<FragmentProcessor> expression) const {
    cachedReexpression = std::move(expression);
  }

  FragmentProcessor* getCachedReexpression() const {
    return cachedReexpression.get();
  }

  PlacementPtr<FragmentProcessor> takeCachedReexpression() const {
    return std::move(cachedReexpression);
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

  /**
   * Marks this info as belonging to a draw whose record-time AOT entry committed it to a single
   * fused pass (the chain interpreter or the perlin kernel — the shapes the decomposition planner
   * defers instead of building a plan task). The matcher walks those two families before the
   * other dedicated families for such a draw, preserving the committed kernel choice. Set before
   * the first MatchPermutation call; never mutated afterwards (the cached match outcome shares
   * the flag).
   */
  void markFusedPassCommitted() {
    fusedPassCommitted = true;
  }

  bool isFusedPassCommitted() const {
    return fusedPassCommitted;
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
  CullMode cullMode = CullMode::None;
  DepthStencilDescriptor depthStencil = {};
  uint32_t colorWriteMask = ColorWriteMask::All;
  bool fusedPassCommitted = false;

  void updateProcessorIndices();

  BytesKey buildProgramKey() const;

  // The cached program key (see programKey()): an idempotence cache like cachedMatch, invalidated
  // by the pipeline-state setters.
  mutable std::optional<BytesKey> cachedProgramKey;

  // The cached MatchPermutation outcome (see PermutationMatch.h): filled by the first caller and
  // reused by every later lookup of the same info. Mutable because the lookup entry takes a
  // const info and the cache is an implementation detail of idempotence, not program state.
  mutable std::optional<PermutationMatchOutcome> cachedMatch;

  // The re-expressing family's cached expression (see the public accessors above).
  mutable PlacementPtr<FragmentProcessor> cachedReexpression = {};

  friend std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo*,
                                                                PermutationMatchFailure*);
  friend class ProgramWriter;
  friend class RuntimeProgramWriter;
  friend class PrecompiledProgramWriter;
  friend class ExpressionProgramWriter;
  friend class ChainProgramWriter;

  std::vector<SamplerInfo> getSamplers() const;

  void updateUniformDataSuffix(UniformData* vertexUniformData, UniformData* fragmentUniformData,
                               const Processor* processor) const;
};
}  // namespace tgfx
