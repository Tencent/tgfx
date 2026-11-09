/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "GeometryProcessor.h"
#include "gpu/AAType.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/gpu/Attribute.h"

namespace tgfx {

/**
 * HairlineQuadGeometryProcessor is used to render hairline quadratic curve segments.
 */
class HairlineQuadGeometryProcessor : public GeometryProcessor {
 public:
  DEFINE_PROCESSOR_CLASS_ID
  static PlacementPtr<HairlineQuadGeometryProcessor> Make(BlockAllocator* allocator,
                                                          const PMColor& color,
                                                          const Matrix& viewMatrix,
                                                          std::optional<Matrix> uvMatrix,
                                                          float coverage, AAType aaType);

  std::string name() const override {
    return "HairlineQuadGeometryProcessor";
  }

  /// The view transform the vertex stage applies to the position.
  const Matrix& getViewMatrix() const {
    return viewMatrix;
  }

  /// The exact paint color the fragment stage reads from the Color uniform.
  PMColor getCommonColor() const {
    return color;
  }

  /// The per-draw coverage scale multiplied into the edge alpha.
  float getCoverage() const {
    return coverage;
  }

  AAType getAAType() const {
    return aaType;
  }

 protected:
  HairlineQuadGeometryProcessor(const PMColor& color, const Matrix& viewMatrix,
                                std::optional<Matrix> uvMatrix, float coverage, AAType aaType);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  PMColor color = {};
  Matrix viewMatrix = {};
  std::optional<Matrix> uvMatrix = std::nullopt;
  float coverage = 1.0f;
  AAType aaType = AAType::None;

  Attribute position = {};
  Attribute hairQuadEdge = {};
};

}  // namespace tgfx
