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

#include <optional>
#include "core/utils/BlockAllocator.h"
#include "gpu/AAType.h"
#include "gpu/VertexProvider.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
struct RectRecord {
  RectRecord(const Rect& rect, const Matrix& viewMatrix, const PMColor& color = {})
      : rect(rect), viewMatrix(viewMatrix), color(color) {
  }

  Rect rect;
  Matrix viewMatrix;
  PMColor color;
};

enum class UVSubsetMode { None, SubsetOnly, RoundOutAndSubset };

enum class UVCoordMode { Auto, Required, Disabled };

/**
 * RectsVertexProvider is a VertexProvider that provides vertices for drawing rectangles.
 */
class RectsVertexProvider : public VertexProvider {
 public:
  /**
   * Creates a new RectsVertexProvider from a single rect.
   */
  static PlacementPtr<RectsVertexProvider> MakeFrom(BlockAllocator* allocator, const Rect& rect,
                                                    AAType aaType);

  /**
   * Creates a new RectsVertexProvider from a list of rect records.
   */
  static PlacementPtr<RectsVertexProvider> MakeFrom(BlockAllocator* allocator,
                                                    std::vector<PlacementPtr<RectRecord>>&& rects,
                                                    std::vector<PlacementPtr<Rect>>&& uvRects,
                                                    AAType aaType, bool needUVCoord,
                                                    UVSubsetMode subsetMode,
                                                    std::vector<PlacementPtr<Stroke>>&& strokes);

  /**
   * Returns the number of rects in the provider.
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
   * Returns true if the provider generates UV coordinates.
   */
  bool hasUVCoord() const {
    return bitFields.hasUVCoord;
  }

  /**
   * Returns true if the provider generates colors.
   */
  bool hasColor() const {
    return bitFields.hasColor;
  }

  /**
   * Returns the line join type if stroke is enabled, nullopt otherwise.
   */
  std::optional<LineJoin> lineJoin() const {
    return _lineJoin;
  }

  /**
   * Returns the first rect in the provider.
   */
  const Rect& firstRect() const {
    return rects.front()->rect;
  }

  /**
   * Returns the first matrix in the provider. If no matrix record exists, a identity matrix is
   * returned.
   */
  const Matrix& firstMatrix() const {
    return rects.front()->viewMatrix;
  }

  /**
   * Returns the first color in the provider. If no color record exists, a white color is returned.
   */
  PMColor firstColor() const {
    return rects.front()->color.makeColorSpace(ColorSpace::DisplayP3());
  }

  /**
   * Returns true if the provider generates subset rects.
   */
  bool hasSubset() const {
    return static_cast<UVSubsetMode>(bitFields.subsetMode) != UVSubsetMode::None;
  }

 protected:
  PlacementArray<RectRecord> rects = {};
  PlacementArray<Rect> uvRects = {};
  std::optional<LineJoin> _lineJoin = std::nullopt;
  struct {
    uint8_t aaType : 2;
    bool hasUVCoord : 1;
    bool hasColor : 1;
    uint8_t subsetMode : 2;
  } bitFields = {};

  RectsVertexProvider(PlacementArray<RectRecord>&& rects, PlacementArray<Rect>&& uvRects,
                      AAType aaType, bool hasUVCoord, bool hasColor, UVSubsetMode subsetMode,
                      std::shared_ptr<BlockAllocator> reference);
};
}  // namespace tgfx
