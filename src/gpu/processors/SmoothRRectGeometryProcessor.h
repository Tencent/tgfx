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
 * SmoothRRectGeometryProcessor renders round rectangles with corner smoothness. It partitions
 * each rrect into corner and non-corner regions. Non-corner regions use axis-aligned edge
 * distances; corner regions use implicit signs for NonAA fills and analytic distance evaluation
 * otherwise.
 */
class SmoothRRectGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<SmoothRRectGeometryProcessor> Make(
      BlockAllocator* allocator, int width, int height, bool antiAlias,
      std::optional<PMColor> commonColor, bool stroke, bool unscaledGeometry);

  std::string name() const override {
    return "SmoothRRectGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  SmoothRRectGeometryProcessor(int width, int height, bool antiAlias,
                               std::optional<PMColor> commonColor, bool stroke,
                               bool unscaledGeometry);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute inPosition;    // device-space position after applying the view matrix (2 floats)
  Attribute inLocalCoord;  // local coordinates (2 floats)
  Attribute inXRadii;      // per-corner x radii in [TL, TR, BR, BL] order (4 floats)
  Attribute inYRadii;      // per-corner y radii in [TL, TR, BR, BL] order (4 floats)
  Attribute inSmoothness;  // corner smoothness shared by the four corners (1 float)
  Attribute inRectBounds;  // rect bounds: left, top, right, bottom (4 floats)

  // Optional attributes.
  Attribute inColor;  // color (4 bytes as UByte4Normalized)
  Attribute inHalfStrokeWidth;  // half stroke width, stroke variant only (1 float)
  // Reciprocals of the view matrix's axis scales, which convert unscaled local distances to the
  // scaled local frame where the AA ramp is defined. Required only for AA unscaled geometry; see
  // unscaledGeometry (2 floats).
  Attribute inInvScale;

  int width = 1;
  int height = 1;
  bool antiAlias = true;
  bool stroke = false;
  // Geometry attributes normally have the view matrix's axis scales baked in. Non-uniformly scaled
  // strokes instead keep the unscaled local geometry, because no single half-width in the scaled
  // frame can represent their direction-dependent widths. Distances are then converted to the
  // scaled local frame for AA, so both variants evaluate AA in the same frame.
  bool unscaledGeometry = false;
  std::optional<PMColor> commonColor = std::nullopt;
};
}  // namespace tgfx
