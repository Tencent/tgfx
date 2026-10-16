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
 * SmoothRRectGeometryProcessor renders filled round rectangles with corner smoothness, in either
 * AA or NonAA mode. Each rrect is drawn as a single quad and the smoothed corner contour is
 * evaluated analytically in the fragment shader from per-corner circular radii. Strokes and
 * elliptical smoothed corners are not supported; callers fall back to the CPU path for those.
 */
class SmoothRRectGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<SmoothRRectGeometryProcessor> Make(BlockAllocator* allocator, int width,
                                                         int height, float smoothness,
                                                         bool antiAlias,
                                                         std::optional<PMColor> commonColor);

  std::string name() const override {
    return "SmoothRRectGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  SmoothRRectGeometryProcessor(int width, int height, float smoothness, bool antiAlias,
                               std::optional<PMColor> commonColor);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  // Vertex attributes - declared in the same order as vertex data layout.
  Attribute inPosition;    // position (2 floats)
  Attribute inLocalCoord;  // local coordinates (2 floats)
  Attribute inRadii;       // per-corner circular radii in [TL, TR, BR, BL] order (4 floats)
  Attribute inRectBounds;  // rect bounds: left, top, right, bottom (4 floats)
  Attribute inColor;       // optional color (4 bytes as UByte4Normalized)

  int width = 1;
  int height = 1;
  float smoothness = 0.0f;
  bool antiAlias = true;
  std::optional<PMColor> commonColor = std::nullopt;
};
}  // namespace tgfx
