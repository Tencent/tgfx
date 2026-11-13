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
#include "gpu/SamplerState.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/Program.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/SamplingOptions.h"

namespace tgfx {

class BlockAllocator;
class FragmentProcessor;
class TextureProxy;

/**
 * One sampler the draw's expression consumes, recorded at lowering in the tree walk's order
 * (parent before children, colors before coverages). The proxy resolves to a texture at bind
 * time: an upload that has not landed yet contributes no sampler (or, for a processor whose
 * declared count is view-independent, a null entry), and a YUV proxy expands to its planes.
 * This is the data the runtime sampler list is built from, so no processor tree is read then.
 */
struct AOTSamplerRef {
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  SamplerState state = {};
  /// The entry came from a coverage processor (the writers' dummy-hole position reads it).
  bool coverageSide = false;
  /// The processor's declared sampler count does not depend on the view being instantiated
  /// (GlassRefraction declares one, the glass shape declares one or two); such an entry occupies
  /// a slot even before the texture exists. A view-derived count instead contributes nothing.
  bool countsWithoutView = false;
};

/** What one coverage term of a draw is, decided once at lowering time by what the term is. */
enum class AOTCoverageTermKind {
  /// An analytic rect/rrect clip: a chain of RectCoverage/RRectCoverage nodes rooted at the
  /// geometry-coverage unit. True coverage — rides the XP's coverage input for every blend mode.
  Analytic,
  /// An alpha-only, non-perspective device-space texture mask: the draw's mask port.
  DeviceMask,
  /// The AtlasText glyph, sourced from the maskCoord attribute. True coverage for an alpha-only
  /// atlas; a color atlas's glyph replaces the output color, which only the dedicated atlas
  /// kernel carries (the chain refuses such a leaf).
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
  AOTEffectGraph color = {};
  std::vector<AOTCoverageTerm> coverage = {};
  /// The samplers the expression consumes, in the tree walk's enumeration order. Recorded at
  /// lowering so the runtime sampler list and the writers read the IR instead of the processors.
  std::vector<AOTSamplerRef> samplers = {};
  BlendMode blendMode = BlendMode::SrcOver;
  /// The sizes of the processor lists this IR was lowered from. The color graph cannot change
  /// after record time, so a color-count mismatch means the IR does not describe this draw. The
  /// coverage list may grow at prepare (a rasterized shape attaches its mask only once the shape
  /// proxy exists); `coverageFPCount` is the watermark DrawOp::appendGrownCoverage extends the
  /// terms from, so the IR stays the draw's complete expression instead of being dropped.
  size_t colorFPCount = 0;
  size_t coverageFPCount = 0;
  /**
   * The compiled Chain interpreter program of this expression. A plan pass carries it from the pass
   * build (admission already compiled the same expression); a plain draw fills it on its first
   * match, so a repeated prepare of the same op reads it back instead of recompiling. Mutable only
   * for that write-once memo: the matcher holds a const ProgramInfo, and an idempotent cache is not
   * draw semantics. Two distinct draws never share an IR, so cross-draw repeats are the program
   * cache's problem, not this one.
   */
  mutable std::optional<AOTProgram> chainProgram = std::nullopt;

  /**
   * The YUV family's compiled color-grade tail. It occupies its own slot rather than sharing
   * chainProgram because the two resolvers accept different graphs: a memo written by one must
   * never answer the other's resolution, or the YUV matcher's budget probe and the chain's serving
   * decision would read each other's program.
   */
  mutable std::optional<AOTProgram> yuvProgram = std::nullopt;

  /// The compiled Chain program this IR carries, or null when it carries none. Only the Chain form
  /// counts as a prepared expression: a plan pass build and a chain draw's own match are the two
  /// producers, and the YUV tail never answers for them.
  const AOTProgram* carriedChainProgram() const {
    return chainProgram.has_value() ? &*chainProgram : nullptr;
  }
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
 * Lowers one draw's color processors and coverage processors into a DrawIR (no glyph term: an
 * AtlasText draw lowers through LowerDrawWithAtlas, whose glyph leaf comes from the op itself).
 * Compose is flattened recursively and every leaf is classified by what it is, not by its
 * position: a leaf RectEffect or RRectEffect is Analytic, an alpha-only DeviceSpaceTextureEffect
 * is DeviceMask (a perspective one lowers with its flag set), and anything else is Subtree. A
 * term whose analytic leaves fail to lower falls back to a Subtree term as a whole, matching the
 * previous whole-term acceptance. The lowering is total over the coverage expression — a second
 * mask or subtree lowers into its own term — and returns nullopt only when the expression is
 * semantically inexpressible (an unsupported processor, a mask that is not alpha-only). The shape
 * limits — at most one mask, one subtree, an affine mask matrix, the analytic slot capacities —
 * are the compile's rejections (MergeCoverageTerms, ValidateSlots): admission equals compile
 * success. allocator places the synthesized unit-color processor.
 */
std::optional<DrawIR> LowerDraw(const std::vector<const FragmentProcessor*>& colorProcessors,
                                const std::vector<const FragmentProcessor*>& coverageProcessors,
                                BlendMode blendMode, BlockAllocator* allocator);

/**
 * Appends the samplers of a coverage processor list that was attached after record time (a
 * rasterized shape's mask grown at prepare) to the draw's sampler record, in the tree walk's
 * order. The terms themselves are lowered separately (see DrawOp::appendGrownCoverage), so the
 * sampler record must be extended in lockstep or the bind loses the mask texture.
 */
void AppendCoverageSamplers(const std::vector<const FragmentProcessor*>& coverageProcessors,
                            DrawIR* ir);

/**
 * The record-time entry: same classification and rejections as LowerDraw, but the glyph leaf is
 * synthesized from the caller's atlas facts — an AtlasText op holds them from construction, so
 * its IR carries the glyph term without waiting for the prepare-time geometry processor. The
 * coverage-count gate is the atlas one (at most one coverage processor beside the glyph). A null
 * proxy returns nullopt; both atlas flavors lower (the glyph leaf's parameters carry the atlas's
 * alpha-only fact, the chain refuses a color glyph, the dedicated atlas kernel carries it).
 */
std::optional<DrawIR> LowerDrawWithAtlas(
    const std::vector<const FragmentProcessor*>& colorProcessors,
    const std::vector<const FragmentProcessor*>& coverageProcessors,
    const AOTAtlasGlyphSource& atlas, BlendMode blendMode, BlockAllocator* allocator);

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
