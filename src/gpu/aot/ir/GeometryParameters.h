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
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include "gpu/AAType.h"
#include "gpu/SamplerState.h"
#include "gpu/aot/ir/GeometryKind.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

class ProgramInfo;
class TextureProxy;

/// The geometry facts a declared family's writers upload: everything the module walk reads off
/// the draw's geometry, snapshotted into one plain value. The writers address this instead of the
/// concrete geometry-processor type, so the geometry stays one data source whatever object
/// produced it. Fields a geometry does not use keep their defaults.
struct AOTGeometryParameters {
  /// Which geometry produced these facts: the matching layer's one classification and the
  /// diagnostic signature read it.
  GeometryKind kind = GeometryKind::Other;
  /// The paint color uniform and its flag (the exact color; per-vertex colors ride the attributes).
  PMColor commonColor = PMColor::Transparent();
  bool hasCommonColor = false;
  /// The view transform of the geometries that project local coordinates (rect, mesh, hairline).
  Matrix viewMatrix = Matrix::I();
  /// The sampling transform of the geometries that carry one inline (quad-per-edge-AA). Module
  /// coordinate matrices stay with their modules (TiledTexture, Texture).
  Matrix uvMatrix = Matrix::I();
  /// Whether the draw carries an inline sampling transform at all (the attribute layout and the
  /// matchers distinguish absent from identity).
  bool hasUVMatrix = false;
  /// Whether the inline transform has a perspective row (part of the program key).
  bool hasUVPerspective = false;
  AAType aaType = AAType::None;
  /// The hairline families' constant coverage factor.
  float coverage = 1.f;
  bool isStroke = false;
  /// The atlas color format (the glyph leaf's AlphaOnly uniform).
  bool isAlphaOnly = false;
  /// The mesh's per-vertex color flag (its HasCommonColor is the complement).
  bool hasColors = false;
  /// The mesh's texture-coordinate flag.
  bool hasTexCoords = false;
  /// The atlas's AA coverage attribute flag (AAType::Coverage).
  bool hasCoverage = false;
  /// The quad geometry's subset attribute flag.
  bool hasSubset = false;
  /// The atlas the glyph attributes index into.
  std::shared_ptr<TextureProxy> atlasProxy = nullptr;
  /// The atlas sampler state (the glyph draws' sampling options).
  SamplerState atlasSampler = {};
};

}  // namespace tgfx
