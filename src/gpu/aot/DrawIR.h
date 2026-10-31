/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <vector>
#include "gpu/aot/AOTEffectDecomposer.h"
#include "gpu/aot/EffectGraph.h"
#include "gpu/aot/Program.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/SamplingOptions.h"

namespace tgfx {

class BlockAllocator;
class FragmentProcessor;
class GeometryProcessor;
class TextureProxy;
class XferProcessor;

/** What one coverage term of a draw is, decided once at lowering time by what the term is. */
enum class AOTCoverageTermKind {
  /// An analytic rect/rrect clip: a chain of RectCoverage/RRectCoverage nodes rooted at the
  /// geometry-coverage unit. True coverage — rides the XP's coverage input for every blend mode.
  Analytic,
  /// An alpha-only, non-perspective device-space texture mask: the draw's mask port.
  DeviceMask,
  /// The AtlasText glyph mask, sourced from the maskCoord attribute. True coverage.
  Glyph,
  /// A general pointwise coverage subtree rooted at the geometry-coverage unit: mask-filter
  /// semantics, which modulate the source rather than the coverage.
  Subtree,
};

/** One classified coverage term. Holds plain data only — no FragmentProcessor pointers. */
struct AOTCoverageTerm {
  AOTCoverageTermKind kind = AOTCoverageTermKind::Analytic;
  /// Analytic: GeometryCoverage -> a chain of Rect/RRectCoverage nodes. Subtree: the general
  /// pointwise subtree rooted at the GeometryCoverage unit. Empty for DeviceMask.
  AOTEffectGraph graph = {};
  AOTNodeID root = AOTNodeID::Invalid();
  /// DeviceMask: the mask leaf's sampler data (alpha-only, non-perspective, device-sampled).
  AOTTextureParameters mask = {};
};

/**
 * One draw's lowered expression: the color graph plus the classified coverage terms. This is the
 * single semantic source the precompiled path converges on — the coverage classification below is
 * the ONE place a coverage effect's semantics (true coverage versus source modulation) are
 * decided, replacing the per-shape enumerations it used to be spread over.
 */
struct DrawIR {
  /// The draw's geometry processor. The geometry descriptor (kind, layout, per-draw parameters)
  /// arrives with the geometry contracts; until then the processor travels with the IR.
  const GeometryProcessor* geometryProcessor = nullptr;
  AOTEffectGraph color = {};
  std::vector<AOTCoverageTerm> coverage = {};
  const XferProcessor* xferProcessor = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  /**
   * Plan-pass serving: the fused pass descriptor the color graph was cut for (kernel, node list,
   * profile, dependencies). A pass IR carries it so its processor is rebuilt exactly as the
   * planner fit it — never re-planned. A draw IR leaves it empty and its single pass is planned
   * on demand. Intermediate passes carry no coverage terms; the TERMINAL pass of a draw's plan
   * carries the draw's terms, which its chain program merges exactly as the plain route's
   * single-pass serving does (see AOTPlanExecutor::Make).
   */
  std::optional<AOTPassDescriptor> passDescriptor = std::nullopt;
  /**
   * The draw's compiled chain program, filled on first use by the plain route's serving (the
   * matcher's walk or the writer's fallback) and reused across the SAME op's repeated prepares —
   * a plan-task fallback rerun or a probe re-preparation would otherwise recompile the identical
   * expression. Mutable like the info caches: an idempotence detail, not draw semantics. Two
   * distinct draws never share an IR, so cross-draw compilation repeats are the program-signature
   * cache's problem (P5.0c), not this one. A pass IR leaves it empty.
   */
  mutable std::optional<AOTProgram> compiledProgram = std::nullopt;
};

/**
 * The atlas facts a record-time glyph-leaf synthesis needs: an AtlasText op owns its atlas proxy
 * and sampling from construction, so the record-time LowerDrawWithAtlas entry takes them directly
 * (the prepare-time LowerDraw entry takes the same facts from the AtlasText geometry processor).
 */
struct AOTAtlasGlyphSource {
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  SamplingOptions sampling{};
};

/**
 * Lowers one draw's whole expression — its color processors, its coverage processors and the
 * glyph leaf an AtlasText geometry processor contributes — into a DrawIR. Compose is flattened
 * recursively and every leaf is classified by what it is, not by its position: a leaf RectEffect
 * or RRectEffect is Analytic, an alpha-only non-perspective DeviceSpaceTextureEffect is DeviceMask,
 * and anything else is Subtree (at most one). A term whose analytic leaves fail to lower falls
 * back to a Subtree term as a whole, matching the previous whole-term acceptance. A null geometry
 * processor contributes no glyph leaf (an AtlasText processor never is). Returns nullopt when the
 * expression does not lower (an unsupported processor, a mask that is not alpha-only or carries
 * perspective, more than one subtree or device mask, or a coverage count the chain never
 * accepted). allocator places the synthesized unit-color and atlas-leaf processors.
 */
std::optional<DrawIR> LowerDraw(const std::vector<const FragmentProcessor*>& colorProcessors,
                                const std::vector<const FragmentProcessor*>& coverageProcessors,
                                const GeometryProcessor* geometryProcessor,
                                const XferProcessor* xferProcessor, BlendMode blendMode,
                                BlockAllocator* allocator);

/**
 * The record-time entry: same classification and rejections as LowerDraw, but the glyph leaf is
 * synthesized from the caller's atlas facts — an AtlasText op holds them from construction, so
 * its IR carries the glyph term without waiting for the prepare-time geometry processor. The
 * coverage-count gate is the atlas one (at most one coverage processor beside the glyph). A null
 * or non-alpha-only proxy returns nullopt: such draws keep the plain path, exactly as before.
 */
std::optional<DrawIR> LowerDrawWithAtlas(
    const std::vector<const FragmentProcessor*>& colorProcessors,
    const std::vector<const FragmentProcessor*>& coverageProcessors,
    const AOTAtlasGlyphSource& atlas, const XferProcessor* xferProcessor, BlendMode blendMode,
    BlockAllocator* allocator);

/**
 * Diagnostic counters over LowerDraw: how many successful lowerings have run since the last
 * reset. The single-lowering invariant — a draw is lowered once at record time and its IR is
 * carried through prepare, with no re-lowering at match or upload time — is assertable with
 * them. Diagnostic only; nothing on the render path reads these.
 */
uint64_t AOTLowerDrawCount();
void ResetAOTLowerDrawCount();

/**
 * The coverage half of LowerDraw alone: flattens Compose recursively and classifies every leaf
 * into coverage terms (the same rules LowerDraw applies — see its doc). glyphLeaf, when given,
 * marks one of the coverage processors as the AtlasText glyph leaf (the caller synthesized it
 * from the geometry processor). Returns nullopt on the same rejections LowerDraw has.
 */
std::optional<std::vector<AOTCoverageTerm>> LowerCoverageTerms(
    const std::vector<const FragmentProcessor*>& coverageProcessors,
    const FragmentProcessor* glyphLeaf, BlockAllocator* allocator);

}  // namespace tgfx
