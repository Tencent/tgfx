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
 * ComplexEllipseGeometryProcessor renders complex round rects (per-corner independent radii) with
 * antialiasing. It extends the standard 9-patch ellipse approach by carrying per-vertex signed
 * edge distances in addition to the ellipse offsets. The fragment shader then dispatches on the
 * offset signs to pick a coverage formula per region: the ellipse SDF inside corner regions,
 * linear edge distance along top/bottom/left/right edges, and full coverage in the interior.
 * This avoids the cross-corner interpolation artifacts that would otherwise appear when corners
 * have different radii.
 */
class ComplexEllipseGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<ComplexEllipseGeometryProcessor> Make(BlockAllocator* allocator, int width,
                                                            int height, bool stroke,
                                                            std::optional<PMColor> commonColor);

  std::string name() const override {
    return "ComplexEllipseGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  ComplexEllipseGeometryProcessor(int width, int height, bool stroke,
                                  std::optional<PMColor> commonColor);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute inPosition;
  Attribute inColor;
  Attribute inEllipseOffset;
  Attribute inEllipseRadii;
  // Signed distance to the two adjacent edges of the assigned corner.
  Attribute inEdgeDist;
  // Half stroke width in x and y directions (stroke mode only).
  Attribute inStrokeWidth;

  int width = 1;
  int height = 1;
  bool stroke = false;
  std::optional<PMColor> commonColor = std::nullopt;
};
}  // namespace tgfx
