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
  /// The paint color uniform and its flag (the exact color; per-vertex colors ride the attributes).
  PMColor commonColor = PMColor::Transparent();
  bool hasCommonColor = false;
  /// The view transform of the geometries that project local coordinates (rect, mesh, hairline).
  Matrix viewMatrix = Matrix::I();
  /// The sampling transform of the geometries that carry one inline (quad-per-edge-AA). Module
  /// coordinate matrices stay with their modules (TiledTexture, Texture).
  Matrix uvMatrix = Matrix::I();
  AAType aaType = AAType::None;
  /// The hairline families' constant coverage factor.
  float coverage = 1.f;
  bool isStroke = false;
  /// The atlas color format (the glyph leaf's AlphaOnly uniform).
  bool isAlphaOnly = false;
  /// The mesh's per-vertex color flag (its HasCommonColor is the complement).
  bool hasColors = false;
  /// The atlas the glyph attributes index into.
  std::shared_ptr<TextureProxy> atlasProxy = nullptr;
};

}  // namespace tgfx
