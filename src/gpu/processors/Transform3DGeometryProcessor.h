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

#include "GeometryProcessor.h"
#include "gpu/AAType.h"
#include "tgfx/core/Matrix3D.h"

namespace tgfx {

/**
 * A geometry processor for rendering 3D transformed quads with optional per-edge anti-aliasing.
 */
class Transform3DGeometryProcessor : public GeometryProcessor {
 public:
  /**
   * Creates a Transform3DGeometryProcessor instance with the specified parameters.
   */
  static PlacementPtr<Transform3DGeometryProcessor> Make(BlockAllocator* allocator, AAType aa,
                                                         const Matrix3D& matrix,
                                                         const Vec2& ndcScale,
                                                         const Vec2& ndcOffset);

  std::string name() const override {
    return "Transform3DGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit Transform3DGeometryProcessor(AAType aa, const Matrix3D& transform, const Vec2& ndcScale,
                                        const Vec2& ndcOffset);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute position = {};

  Attribute coverage = {};

  AAType aa = AAType::None;

  /**
   * The transformation matrix from local space to clip space.
   */
  Matrix3D matrix = Matrix3D::I();

  /**
   * The scaling and translation parameters in NDC space. After the projected model's vertex
   * coordinates are transformed to NDC, ndcScale is applied for scaling, followed by ndcOffset for
   * translation. These two properties allow any rectangular region of the projected model to be
   * mapped to any position within the target texture.
   */
  Vec2 ndcScale = Vec2(0.f, 0.f);
  Vec2 ndcOffset = Vec2(0.f, 0.f);
};

}  // namespace tgfx
