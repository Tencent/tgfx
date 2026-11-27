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

#include <cstddef>
#include <memory>
#include <vector>
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/layers/LayerPaint.h"
#include "tgfx/layers/vectors/ColorSource.h"
#include "tgfx/layers/vectors/ScaleMode.h"

namespace tgfx {

/** Ceilings for vector-owned copies and expansion, including checked derived metadata overhead.
 * Native font/provider/backend allocation retains its own system-boundary contract.
 */
struct VectorPrepareLimits {
  /** Maximum prepared draws, geometries, painters, and capture entries for each category. */
  size_t maxDraws = 262144;
  /** Maximum cumulative known source-point and gradient-stop copies. */
  size_t maxGeometryPoints = 16777216;
  /** Maximum cumulative glyph entries copied or built during preparation. */
  size_t maxGlyphs = 1048576;
  /** Maximum cumulative logical repeater copies across the traversal. */
  size_t maxCopies = 16384;
  /** Maximum nested group depth; the root contents are depth zero. */
  size_t maxDepth = 256;
};

/** Cumulative checked reservations for one preparation, including work that emitted no callbacks. */
struct VectorPrepareUsage {
  /** The quota that rejected preparation; None when no quota was exhausted. */
  enum class Limit { None, Draws, GeometryPoints, Glyphs, Copies, Depth };

  /** First exhausted ceiling, so callers can report the actual budget instead of a generic failure. */
  Limit limit = Limit::None;
  /** Paint and own-geometry emission reservations, not the number of sink callbacks. */
  size_t draws = 0;
  /** Geometry creations and clones. */
  size_t geometries = 0;
  /** Painter creations and clones. */
  size_t painters = 0;
  /** Captured geometry entries, including entries copied by cloned painters. */
  size_t captures = 0;
  /** Known source/path/font-outline, gradient-stop, and dash-array copy reservations. */
  size_t geometryPoints = 0;
  /** Glyph input, expansion, blob construction, wrapper, and selector-cache reservations. */
  size_t glyphs = 0;
  /** Logical repeater copies across all groups, not the number of resulting draws. */
  size_t copies = 0;
  /** Owned metadata entries reserved independently of geometryPoints. */
  size_t metadataEntries = 0;
  /** Highest accepted group depth, a peak rather than a cumulative reservation. */
  size_t peakDepth = 0;
};

/** One actual painter-scope alpha multiplication, including zero-valued factors. */
struct VectorAlphaFactor {
  enum class Kind { Painter, Group, Repeater };
  Kind kind = Kind::Painter;
  /** Opaque endpoint identity; it is not a repeated-instance identity. */
  const void* sourceTag = nullptr;
  float alpha = 1;
  /** Current-frame runtime style/group/repeater identity; never a cross-frame key or dereferenced. */
  const void* runtimeEndpoint = nullptr;
};

/** An endpoint whose applied geometry effect contributes to a prepared draw. */
struct VectorGeometryDependency {
  enum class Kind {
    GroupTransform,
    TextModifier,
    TextSelector,
    TextPath,
    Merge,
    Repeater,
    TrimPath,
    RoundCorner
  };
  Kind kind = Kind::GroupTransform;
  /** Opaque dependency endpoint identity, never dereferenced by tgfx. */
  const void* sourceTag = nullptr;
  /** Current-frame runtime modifier/group/selector identity, only for binding-object comparison. */
  const void* runtimeEndpoint = nullptr;
};

/** Owned color-source parameters captured before any prepared draw is delivered. */
struct VectorColorSnapshot {
  enum class Kind { Solid, Linear, Radial, Conic, Diamond, ImagePattern };

  /** Captures source parameters; source must identify a supported non-null ColorSource. */
  static VectorColorSnapshot Capture(const ColorSource* source);

  /** Selects the applicable color, gradient, or image fields below. */
  Kind kind = Kind::Solid;
  /** Solid RGBA color, including an overlay's override weight when applicable. */
  Color color = Color::Transparent();
  /** Authored color space of `color`; the snapshot keeps it because RGBA alone cannot say it. */
  VectorColorSpace colorSpace = VectorColorSpace::SRGB;
  /** Owned gradient stops and their authored positions; empty positions mean evenly spaced. */
  std::vector<Color> colors = {};
  std::vector<float> positions = {};
  /** Authored space of each stop; empty means every stop uses `colorSpace`. */
  std::vector<VectorColorSpace> colorSpaces = {};
  /** Linear endpoints in color-source local space. */
  Point start = Point::Zero();
  Point end = Point::Zero();
  /** Radial, conic, or diamond center and radial/diamond radius in source local space. */
  Point center = Point::Zero();
  float radius = 0;
  /** Conic angular range in degrees. */
  float startAngle = 0;
  float endAngle = 0;
  /** Authored source-local transformation, before per-geometry fitting. */
  Matrix matrix = Matrix::I();
  /** Retained immutable image and its sampling configuration. */
  std::shared_ptr<Image> image = nullptr;
  TileMode tileModeX = TileMode::Decal;
  TileMode tileModeY = TileMode::Decal;
  SamplingOptions sampling = {};
  ScaleMode scaleMode = ScaleMode::None;
  /** Whether the source was fitted into each prepared geometry's bounds. */
  bool fitsToGeometry = false;
};

/** Parameters corresponding to exactly one LayerPaint in a prepared operation. */
struct VectorPaintSnapshot {
  enum class Role { Base, Overlay };

  /** Base uses the authored source; Overlay uses the captured solid override color. */
  Role role = Role::Base;
  VectorColorSnapshot colorSource = {};
  /** Whether the prepared shader satisfies the renderer's constant-color folding decision. */
  bool shaderIsSolid = false;
  /** Frozen shader RGBA when shaderIsSolid, before scope/glyph alpha multiplication. */
  Color shaderSolidColor = Color::Transparent();
  /** Style alpha including enclosing groups and repeater-copy alpha. */
  float scopePainterAlpha = 1;
  /** Owned actual painter/group/repeater factors whose product is scopePainterAlpha. */
  std::vector<VectorAlphaFactor> alphaFactors = {};
  /** Glyph alpha multiplier; one for a shape draw. */
  float glyphAlpha = 1;
  /** Overlay shader alpha; base shader alpha is not attenuated by this weight. */
  float overrideWeight = 0;
  /** Exact bounds supplied to the fit hook, in the prepared paint's local space. */
  Rect fitBounds = Rect::MakeEmpty();
  /** Actual fit matrix, or identity when fitting is disabled or this paint is an overlay. */
  Matrix fitMatrix = Matrix::I();
};

/** One step in the owned provenance of a repeated geometry. */
struct VectorCopyProvenance {
  /** Opaque repeater endpoint identity; never dereferenced by tgfx. */
  const void* sourceTag = nullptr;
  /** Logical copy index, independent of BelowOriginal/AboveOriginal draw ordering. */
  size_t copyIndex = 0;
  /** Copy index plus authored offset, and the actual matrix and alpha for this copy. */
  float progress = 0;
  Matrix matrix = Matrix::I();
  float alpha = 1;
  /** Current-frame originating repeater identity, preserved through nested copying. */
  const void* runtimeEndpoint = nullptr;
};

/** A contributing geometry source, preserving distinct copies through a merge. */
struct VectorGeometryOrigin {
  /** Opaque producing-element identity; distinct origins are never deduplicated. */
  const void* sourceTag = nullptr;
  std::vector<VectorCopyProvenance> copyProvenance = {};
  /** Current-frame originating vector element identity; distinct authored-node uses stay separate. */
  const void* runtimeEndpoint = nullptr;
};

/** Owned prepared geometry and paint state; endpoint pointers are identities, not live data. */
struct VectorDrawOp {
  /** Prepared shape or glyph blob; exactly one is present for an emitted painted draw. */
  std::shared_ptr<Shape> shape = nullptr;
  std::shared_ptr<TextBlob> textBlob = nullptr;
  /** Prepared renderer paints and their owned snapshots, in identical draw order. */
  std::vector<LayerPaint> paints = {};
  std::vector<VectorPaintSnapshot> paintSnapshots = {};
  /** Prepared geometry-to-layer transform used by the recorder. */
  Matrix matrix = Matrix::I();
  /** Geometry transform at painter capture and final geometry-to-layer transform. */
  Matrix capturedInnerMatrix = Matrix::I();
  Matrix finalMatrix = Matrix::I();
  /** Deterministic zero-based draw and final-context geometry ordinals within prepareDraws(). */
  size_t drawOrdinal = 0;
  size_t geometryOrdinal = 0;
  /** Zero-based styled glyph-run ordinal; zero for shape and own-geometry draws. */
  size_t runOrdinal = 0;
  /** Owned nested repeater lineage and all contributing origins, including merged inputs. */
  std::vector<VectorCopyProvenance> copyProvenance = {};
  std::vector<VectorGeometryOrigin> sourceProvenance = {};
  /** Owned applied geometry dependencies, including modifiers applied after painter capture. */
  std::vector<VectorGeometryDependency> dependencies = {};
  /** Callback-only geometry identity; invalid after prepareDraws(), use geometryOrdinal instead. */
  const void* geometry = nullptr;
  /** Opaque producing element and style identities; never dereference them through this op. */
  const void* sourceTag = nullptr;
  const void* painterTag = nullptr;
  /** Current-frame producing element and original style identities, never cross-frame probe keys. */
  const void* sourceEndpoint = nullptr;
  const void* painterEndpoint = nullptr;
  /** Retained endpoint identity only; paintSnapshots contain all frozen source parameters. */
  std::shared_ptr<ColorSource> colorSource = nullptr;
};

/** Receives fully prepared operations; all owned fields may be retained after the callback. */
class VectorDrawSink {
 public:
  virtual ~VectorDrawSink() = default;

  /** Receives op, a prepared shape draw with its paints and corresponding snapshots. */
  virtual void drawShape(const VectorDrawOp& op) = 0;

  /** Receives op, a prepared glyph-run draw with its paints and corresponding snapshots. */
  virtual void drawGlyphRun(const VectorDrawOp& op) = 0;

  /** Receives op, unpainted own geometry with source identity and no implicit default paint. */
  virtual void drawOwnGeometry(const VectorDrawOp& op) = 0;
};

}  // namespace tgfx
