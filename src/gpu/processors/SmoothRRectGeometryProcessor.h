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
 * SmoothRRectGeometryProcessor renders round rectangles with corner smoothness, filled or
 * stroked, in either AA or NonAA mode. Each rrect is drawn as a single quad and the smoothed
 * corner contour is evaluated analytically in the fragment shader from per-corner radii. Segment
 * selection inside a corner uses the true join-point normals in device space; the retained arc
 * uses the exact circle distance for circular corners and a single approximating cubic for
 * elliptical corners. Strokes reuse the same signed distance field: the coverage is computed
 * from the ring distance |dist| - halfStroke, with the cubic solver iterating three times per
 * seed instead of the fill's two. A stroke under a non-uniformly scaling matrix uses the
 * localStroke variant: distances are evaluated in the unscaled local frame and converted to
 * device pixels per fragment via the inverse-transpose matrix before the coverage ramp. The
 * supported envelope (axis ratio cap, stroke width limits per evaluation frame) is enforced by
 * UseDrawPath in Canvas.cpp.
 */
class SmoothRRectGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<SmoothRRectGeometryProcessor> Make(
      BlockAllocator* allocator, int width, int height, float smoothness, bool antiAlias,
      std::optional<PMColor> commonColor, bool elliptical = false, bool stroke = false,
      bool localStroke = false);

  std::string name() const override {
    return "SmoothRRectGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  SmoothRRectGeometryProcessor(int width, int height, float smoothness, bool antiAlias,
                               std::optional<PMColor> commonColor, bool elliptical = false,
                               bool stroke = false, bool localStroke = false);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  // Vertex attributes - declared in the same order as vertex data layout.
  Attribute inPosition;     // position (2 floats)
  Attribute inLocalCoord;   // local coordinates (2 floats)
  Attribute inRadii;        // per-corner circular radii in [TL, TR, BR, BL] order (4 floats)
  Attribute inRadiiY;       // per-corner y radii, elliptical variant only (4 floats)
  Attribute inRectBounds;   // rect bounds: left, top, right, bottom (4 floats)
  Attribute inColor;        // optional color (4 bytes as UByte4Normalized)
  Attribute inStrokeWidth;  // half stroke width, stroke variant only (1 float)
  // Inverse-transpose of the record's view matrix for the AA distance conversion, localStroke
  // variant only (2x2, column-major, 4 floats).
  Attribute inInvTranspose;

  int width = 1;
  int height = 1;
  float smoothness = 0.0f;
  bool antiAlias = true;
  bool elliptical = false;
  bool stroke = false;
  bool localStroke = false;
  std::optional<PMColor> commonColor = std::nullopt;
};
}  // namespace tgfx
