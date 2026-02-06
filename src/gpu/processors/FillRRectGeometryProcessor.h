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

#include <optional>
#include "GeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {
/**
 * FillRRectGeometryProcessor is used to render filled round rectangles using coverage-based
 * antialiasing. It computes vertex positions and arc coverage in the shader using a normalized
 * [-1, +1] coordinate space.
 */
class FillRRectGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<FillRRectGeometryProcessor> Make(BlockAllocator* allocator, int width,
                                                       int height, AAType aaType,
                                                       std::optional<PMColor> commonColor);

  std::string name() const override {
    return "FillRRectGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  FillRRectGeometryProcessor(int width, int height, AAType aaType,
                             std::optional<PMColor> commonColor);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  // Vertex attributes
  Attribute inCornerAndRadius;  // corner (2) + radius_outset (2) = float4
  Attribute inAABloatCoverage;  // aa_bloat_dir (2) + coverage + is_linear = float4
  Attribute inSkew;             // skew matrix (4 floats)
  Attribute inTranslate;        // translate (2 floats)
  Attribute inRadii;            // radii (2 floats), same for all corners
  Attribute inColor;            // optional color

  int width = 1;
  int height = 1;
  AAType aaType = AAType::Coverage;
  std::optional<PMColor> commonColor = std::nullopt;
};
}  // namespace tgfx
