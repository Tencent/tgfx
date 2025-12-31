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

#include "core/utils/BlockAllocator.h"
#include "gpu/AAType.h"
#include "gpu/QuadRecord.h"
#include "gpu/VertexProvider.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * QuadsVertexProvider provides vertex data for rendering a batch of 3D quads with per-edge AA.
 */
class QuadsVertexProvider : public VertexProvider {
 public:
  /**
   * Creates a QuadsVertexProvider from a single rect with all edges marked for AA.
   */
  static PlacementPtr<QuadsVertexProvider> MakeFrom(BlockAllocator* allocator, const Rect& rect,
                                                    AAType aaType, const Color& color = {});

  /**
   * Creates a QuadsVertexProvider from a list of quad records.
   */
  static PlacementPtr<QuadsVertexProvider> MakeFrom(
      BlockAllocator* allocator, std::vector<PlacementPtr<QuadRecord>>&& quads, AAType aaType);

  /**
   * Returns the number of quads in the provider.
   */
  size_t quadCount() const {
    return quads.size();
  }

  /**
   * Returns the AAType of the provider.
   */
  AAType aaType() const {
    return _aaType;
  }

  /**
   * Returns true if the provider generates colors.
   */
  bool hasColor() const {
    return _hasColor;
  }

  /**
   * Returns the first color in the provider.
   */
  const Color& firstColor() const {
    return quads.front()->color;
  }

 protected:
  QuadsVertexProvider(PlacementArray<QuadRecord>&& quads, AAType aaType, bool hasColor,
                      std::shared_ptr<BlockAllocator> reference);

  PlacementArray<QuadRecord> quads = {};
  AAType _aaType = AAType::None;
  bool _hasColor = false;
};

}  // namespace tgfx
