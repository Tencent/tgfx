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

#include "core/ColorSpaceXformSteps.h"
#include "core/utils/BlockAllocator.h"
#include "gpu/AAType.h"
#include "gpu/VertexProvider.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
struct RRectRecord {
  RRectRecord(const RRect& rRect, const Matrix& viewMatrix, Color color = {})
      : rRect(rRect), viewMatrix(viewMatrix), color(color) {
  }

  RRect rRect;
  Matrix viewMatrix;
  Color color;
};

/**
 * RRectsVertexProvider is a VertexProvider that provides vertices for drawing round rectangles.
 */
class RRectsVertexProvider : public VertexProvider {
 public:
  /**
   * Creates a new RRectsVertexProvider from a list of RRect records.
   */
  static PlacementPtr<RRectsVertexProvider> MakeFrom(
      BlockAllocator* allocator, std::vector<PlacementPtr<RRectRecord>>&& rects, AAType aaType,
      std::vector<PlacementPtr<Stroke>>&& strokes,
      std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Returns the number of round rects in the provider.
   */
  size_t rectCount() const {
    return rects.size();
  }

  /**
   * Returns the AAType of the provider.
   */
  AAType aaType() const {
    return static_cast<AAType>(bitFields.aaType);
  }

  /**
   * Returns true if the provider generates colors.
   */
  bool hasColor() const {
    return bitFields.hasColor;
  }

  bool hasStroke() const {
    return bitFields.hasStroke;
  }

  /**
   * Returns true if the provider holds complex RRects with per-corner radii.
   */
  virtual bool isComplex() const = 0;

  /**
   * Returns true if any record has an elliptical corner (radiusX != radiusY). Only smooth
   * records can be elliptical; they use a dedicated shader variant with per-axis radii.
   */
  bool isElliptical() const {
    return bitFields.elliptical;
  }

  /**
   * Returns true if any record is a stroke under a non-uniformly scaling matrix. Such records
   * are evaluated in the unscaled local frame (a baked uniform-width ring cannot express a
   * direction-dependent stroke width) and carry the inverse-transpose matrix per vertex for the
   * AA distance conversion. Only smooth records support it.
   */
  bool hasLocalStroke() const {
    return bitFields.localStroke;
  }

  /**
   * Returns the corner smoothness shared by all records, in [0, 1]. The compositor keeps a single
   * smoothness per batch.
   */
  float smoothness() const {
    return _smoothness;
  }

  /**
   * Returns true if the records carry corner smoothness.
   */
  bool hasSmoothness() const {
    return _smoothness > 0.0f;
  }

  /**
   * Returns the first color in the provider. If no color record exists, a white color is returned.
   */
  const Color& firstColor() const {
    return rects.front()->color;
  }

  const std::shared_ptr<ColorSpace>& dstColorSpace() const {
    return _dstColorSpace;
  }

 protected:
  PlacementArray<RRectRecord> rects = {};
  PlacementArray<Stroke> strokes = {};
  std::shared_ptr<ColorSpace> _dstColorSpace = nullptr;
  float _smoothness = 0.0f;
  struct {
    uint8_t aaType : 2;
    bool hasColor : 1;
    bool hasStroke : 1;
    bool elliptical : 1;
    bool localStroke : 1;
  } bitFields = {};

  RRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType, bool hasColor,
                       PlacementArray<Stroke>&& strokes, std::shared_ptr<BlockAllocator> reference,
                       std::shared_ptr<ColorSpace> colorSpace = nullptr, bool elliptical = false,
                       bool localStroke = false);
};
}  // namespace tgfx
