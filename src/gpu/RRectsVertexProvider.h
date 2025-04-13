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

#include "core/utils/PlacementPtr.h"
#include "gpu/AAType.h"
#include "gpu/VertexProvider.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/RRect.h"

namespace tgfx {
struct RRectPaint {
  RRectPaint(const RRect& rRect, const Matrix& viewMatrix, Color color = Color::White())
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
   * Creates a new RRectsVertexProvider from a list of RRectPaint records.
   */
  static std::unique_ptr<RRectsVertexProvider> MakeFrom(std::vector<PlacementPtr<RRectPaint>> rects,
                                                        AAType aaType, bool useScale);

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

  size_t vertexCount() const override;

  void getVertices(float* vertices) const override;

 private:
  std::vector<PlacementPtr<RRectPaint>> rects = {};
  struct {
    uint8_t aaType : 2;
    bool useScale : 1;
  } bitFields = {};

  RRectsVertexProvider(std::vector<PlacementPtr<RRectPaint>> rects, AAType aaType, bool useScale);
};
}  // namespace tgfx
