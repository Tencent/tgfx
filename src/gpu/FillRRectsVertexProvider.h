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

#include "core/utils/BlockAllocator.h"
#include "gpu/AAType.h"
#include "gpu/RRectsVertexProvider.h"
#include "gpu/VertexProvider.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/RRect.h"

namespace tgfx {
/**
 * FillRRectsVertexProvider provides vertices for drawing filled round rectangles using the
 * FillRRectOp approach. It uses a normalized [-1, +1] coordinate space and generates vertices
 * for coverage-based antialiasing.
 */
class FillRRectsVertexProvider : public VertexProvider {
 public:
  /**
   * Creates a new FillRRectsVertexProvider from a list of RRect records.
   */
  static PlacementPtr<FillRRectsVertexProvider> MakeFrom(
      BlockAllocator* allocator, std::vector<PlacementPtr<RRectRecord>>&& rects, AAType aaType,
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

  /**
   * Returns the first color in the provider. If no color record exists, a white color is returned.
   */
  const Color& firstColor() const {
    return rects.front()->color;
  }

  size_t vertexCount() const override;

  void getVertices(float* vertices) const override;

  const std::shared_ptr<ColorSpace>& dstColorSpace() const {
    return _dstColorSpace;
  }

 private:
  PlacementArray<RRectRecord> rects = {};
  std::shared_ptr<ColorSpace> _dstColorSpace = nullptr;
  struct {
    uint8_t aaType : 2;
    bool hasColor : 1;
  } bitFields = {};

  FillRRectsVertexProvider(PlacementArray<RRectRecord>&& rects, AAType aaType, bool hasColor,
                           std::shared_ptr<BlockAllocator> reference,
                           std::shared_ptr<ColorSpace> colorSpace = nullptr);

  friend class BlockAllocator;
};
}  // namespace tgfx
