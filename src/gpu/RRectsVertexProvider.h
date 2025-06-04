/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/utils/BlockBuffer.h"
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
  static PlacementPtr<RRectsVertexProvider> MakeFrom(BlockBuffer* blockBuffer,
                                                     std::vector<PlacementPtr<RRectRecord>>&& rects,
                                                     AAType aaType, bool useScale,
                                                     std::vector<PlacementPtr<Stroke>>&& strokes);

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

  bool useScale() const {
    return bitFields.useScale;
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
   * Returns the first color in the provider. If no color record exists, a white color is returned.
   */
  const Color& firstColor() const {
    return rects.front()->color;
  }

  size_t vertexCount() const override;

  void getVertices(float* vertices) const override;

 private:
  PlacementArray<RRectRecord> rects = {};
  PlacementArray<Stroke> strokes = {};
  struct {
    uint8_t aaType : 2;
    bool useScale : 1;
    bool hasColor : 1;
    bool hasStroke : 1;
  } bitFields = {};

  RRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType, bool useScale,
                       bool hasColor, PlacementArray<Stroke>&& strokes);

  friend class BlockBuffer;
};
}  // namespace tgfx
