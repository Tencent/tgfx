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
#include "gpu/aot/ir/Program.h"
#include "gpu/aot/ir/XferFacts.h"
#include "gpu/aot/plan/DrawPlan.h"
#include "gpu/aot/select/SelectionScope.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/ColorWriteMask.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class Context;
struct DrawIR;

struct SamplerInfo {
  std::shared_ptr<Texture> texture;
  SamplerState state;
};

/**
 * This immutable object contains information needed to build a shader program and set API state for
 * a draw.
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

  const std::vector<Attribute>& getVertexAttributes() const {
    return getGeometryLayout().vertexAttributes;
  }

  const std::vector<Attribute>& getInstanceAttributes() const {
    return getGeometryLayout().instanceAttributes;
  }

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
   * Records the draw's resolved plan on this info. The selection layer is the only caller: it runs
   * once per draw, right before the program is created from the selected variant.
   */
  void setPlan(const DrawPlan& value) {
    plan = value;
  }

  /// The draw's resolved plan, or null when no selection ran (a draw that did not match).
  const DrawPlan* getPlan() const {
    return plan.has_value() ? &*plan : nullptr;
  }

  /**
   * Narrows the program lookup to the scope this info serves. The default (AnyFamily) is a draw's
   * own expression; a plan pass sets ChainOnly because its expression is already compiled into the
   * carried IR and only the interpreter carries it.
   */
  void setSelectionScope(SelectionScope scope) {
    selectionScope = scope;
  }

  SelectionScope getSelectionScope() const {
    return selectionScope;
  }

  /**
   * The plan's data sources (the texture/gradient/kernel leaves the declared families' writers
   * read), or null when no selection ran.
   */
  const FamilyMatchResult* getSources() const {
    return plan.has_value() ? &plan->sources : nullptr;
  }

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
   * Sets the depth/stencil descriptor that will be applied to the render pipeline so the program
   * cache key reflects the requested stencil state.
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
   * The attempt key: what the draw is before any match ran, computed once per info. The program
   * cache is keyed by MakeProgramKey instead (a program's key carries its matched variant), so this
   * key only addresses the negative cache and the diagnostic records. The setters above invalidate
   * it, so it always reflects the current pipeline state.
   */
  const BytesKey& programKey() const;

  /** Formats a program key as the hex string the diagnostic records carry. */
  static std::string FormatDiagnosticKey(const BytesKey& key);

  /**
   * Returns the GPU backend used by this program's render target.
   */
  Backend backend() const;

  /**
   * The draw's record-time lowered expression, attached by StandardDrawOp::prepare from the op
   * when the draw carries one and its processor list is the unmodified record-time list (no
   * color override). This is an input, not a cache: set once at construction of the info, read by
   * the re-expressing families to skip their second lowering. Null for draws that did not lower,
   * atlas-text draws (whose glyph leaf needs the geometry processor), and every info built with
   * replaced colors.
   */
  void setCarriedIR(const DrawIR* ir) const {
    carriedIR = ir;
  }

  const DrawIR* getCarriedIR() const {
    return carriedIR;
  }

  /**
   * The diagnostics-only effect signature, built by the op from its processor lists (empty when
   * diagnostic recording is off). Nothing that selects, keys or writes a program reads it.
   */
  void setEffectSignature(std::string signature) const {
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

  BytesKey buildProgramKey() const;

  // The cache below is the one deliberate exemption to the no-mutable rule: it holds an
  // idempotent derived value (the same inputs always produce the same result), computed lazily
  // because the key inputs (colorWriteMask, depthStencil, cullMode) are set after construction.
  // No observable behavior depends on whether it is filled, so the object stays semantically
  // immutable.

  // The cached attempt key (see programKey()), invalidated by the pipeline-state setters.
  mutable std::optional<BytesKey> cachedProgramKey;

  // The draw's record-time DrawIR, attached through setCarriedIR() before first use. Mutable only
  // so the attachment can happen right after construction (the pipeline-state setters already
  // run in that window); it is an input, never recomputed, and null unless explicitly attached.
  mutable const DrawIR* carriedIR = nullptr;

  // The diagnostics-only signature (see setEffectSignature).
  mutable std::string effectSignature = {};

  // The draw's single selection result (see getPlan), filled by getProgram()'s first call. An
  // input to the upload, never recomputed; null when no selection ran.
  std::optional<DrawPlan> plan = std::nullopt;

  // The scope getProgram() resolves in: a draw's own expression by default, the compiled chain
  // for a plan pass.
  SelectionScope selectionScope = SelectionScope::AnyFamily;

  friend class ProgramWriter;
  friend class PrecompiledProgramWriter;
  friend class ChainProgramWriter;

  std::vector<SamplerInfo> getSamplers() const;

  /// The number of color-side samplers the draw supplies (the geometry atlas included): the
  /// position an absent device mask leaves a hole at. Read off the IR's sampler record.
  size_t suppliedColorSamplerCount() const;
};
}  // namespace tgfx
