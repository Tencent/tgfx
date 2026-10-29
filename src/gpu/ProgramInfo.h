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
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/GeometryProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/ColorWriteMask.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

/**
 * The matched precompiled family of one program shape, as the permutation matcher resolves it
 * (the variant-only form the family Match functions return is PermutationVariant in
 * ShaderPermutation.h).
 */
struct PermutationMatchResult {
  std::string shaderName;
  uint32_t vertPermutationIndex;
  uint32_t fragPermutationIndex;
};

enum class PermutationMatchFailure {
  None,
  UnsupportedOutputSwizzle,
  NoMatchingRule,
  /// A rule matched, but the draw samples a texture whose read semantics (e.g. the GRAY_8 / RG_88
  /// read swizzle) the matched shader family does not declare in PrecompiledShaderInfo.
  UnsupportedTextureRead,
  // Sub-reasons below all report as NoMatchingRule to the coarse fallback category; they exist so
  // a miss says where it was rejected instead of only "no rule matched". Append only.
  /// The OpenGL entry gate refused the pipeline (bundle profile mismatch or a non-2D sampler).
  GlGateRejected,
  /// The chain path refuses quads that carry a vertex subset attribute.
  QuadSubset,
  /// More than one gradient (or gradient + noise) in a single chain pass.
  GradientOrNoiseBudget,
  /// The draw carries a coverage FP shape the candidate kernel cannot evaluate.
  CoverageShape,
  /// No family matched the geometry processor at all.
  NoMatchingFamily,
  /// The pipeline composed, but that combination is not in the enumerated variant set.
  PermutationNotEnumerated,
  /// Building the AOT graph failed because a processor has no lowering (the default
  /// FragmentProcessor::lowerToAOT).
  MissingLowering,
};

struct SamplerInfo {
  std::shared_ptr<Texture> texture;
  SamplerState state;
};

std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo,
                                                       PermutationMatchFailure* failure = nullptr);

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

  /**
   * Matches this info against the precompiled families and caches the outcome on the info: a draw
   * the prepare-time rewrite probes and then looks up matches once, the later lookup reusing the
   * cached result instead of re-running the family walk. Declared here, defined next to the
   * matcher (PermutationMatcher.cpp) so the family dispatch stays in one file.
   */
  std::optional<PermutationMatchResult> matchAndCache(PermutationMatchFailure* failure = nullptr);

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
   * Sets the depth/stencil descriptor that will be applied to the render pipeline. Draw ops that
   * need stencil testing or writing call this before getProgram() so the pipeline descriptor
   * forwarded by GLSLProgramBuilder picks up the configuration and the program-cache key reflects
   * the requested stencil state.
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

  void updateProcessorIndices();

  BytesKey buildProgramKey() const;

  // The cached permutation match (see matchAndCache): set once by the first matching caller, read
  // by every later lookup of the same info. matchCached distinguishes "matched and missed" from
  // "never matched".
  bool matchCached = false;
  std::optional<PermutationMatchResult> preMatched;
  PermutationMatchFailure preMatchedFailure = PermutationMatchFailure::None;

  friend std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo*,
                                                                PermutationMatchFailure*);

  std::vector<SamplerInfo> getSamplers() const;

  void updateUniformDataSuffix(UniformData* vertexUniformData, UniformData* fragmentUniformData,
                               const Processor* processor) const;
};
}  // namespace tgfx
