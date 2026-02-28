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
  static PlacementPtr<HairlineQuadGeometryProcessor> Make(BlockAllocator* allocator,
                                                          const PMColor& color,
                                                          const Matrix& viewMatrix,
                                                          std::optional<Matrix> uvMatrix,
                                                          float coverage, AAType aaType);

  std::string name() const override {
    return "HairlineQuadGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  HairlineQuadGeometryProcessor(const PMColor& color, const Matrix& viewMatrix,
                                std::optional<Matrix> uvMatrix, float coverage, AAType aaType);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  PMColor color;
  Matrix viewMatrix;
  std::optional<Matrix> uvMatrix;
  float coverage = 1.0f;
  AAType aaType = AAType::None;

  Attribute position;
  Attribute hairQuadEdge;
};

}  // namespace tgfx