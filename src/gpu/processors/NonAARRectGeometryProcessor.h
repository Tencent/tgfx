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

namespace tgfx {
/**
 * NonAARRectGeometryProcessor is used to render round rectangles without antialiasing.
 * It evaluates the round rect shape in the fragment shader using local coordinates.
 * Supports both fill and stroke modes.
 */
class NonAARRectGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<NonAARRectGeometryProcessor> Make(BlockAllocator* allocator, int width,
                                                        int height, bool stroke,
                                                        std::optional<PMColor> commonColor);

  std::string name() const override {
    return "NonAARRectGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  NonAARRectGeometryProcessor(int width, int height, bool stroke,
                              std::optional<PMColor> commonColor);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  // Vertex attributes
  Attribute inPosition;     // position (2 floats)
  Attribute inLocalCoord;   // local coordinates (2 floats)
  Attribute inRadii;        // corner radii (2 floats)
  Attribute inRectBounds;   // rect bounds: left, top, right, bottom (4 floats)
  Attribute inStrokeWidth;  // half stroke width (2 floats, stroke only)
  Attribute inColor;        // optional color

  int width = 1;
  int height = 1;
  bool stroke = false;
  std::optional<PMColor> commonColor = std::nullopt;
};
}  // namespace tgfx
