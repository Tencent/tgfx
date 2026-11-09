/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

namespace tgfx {
class DefaultGeometryProcessor : public GeometryProcessor {
 public:
  DEFINE_PROCESSOR_CLASS_ID
  static PlacementPtr<DefaultGeometryProcessor> Make(BlockAllocator* allocator, PMColor color,
                                                     int width, int height, AAType aa,
                                                     const Matrix& viewMatrix,
                                                     const Matrix& uvMatrix);

  std::string name() const override {
    return "DefaultGeometryProcessor";
  }

  /// Returns the anti-aliasing type. Used by the AOT matcher to select the coverage permutation.
  AAType getAAType() const {
    return aa;
  }

  /// The view transform the vertex stage applies to the position.
  const Matrix& getViewMatrix() const {
    return viewMatrix;
  }

  /// The paint color the fragment stage reads from the Color uniform.
  PMColor getCommonColor() const {
    return commonColor;
  }

  /// The texture-space transform the coord transforms of the FP tree ride on (setTransformDataHelper
  /// concatenates it onto every transform).
  const Matrix& getUVMatrix() const {
    return uvMatrix;
  }

 protected:
  DefaultGeometryProcessor(PMColor color, int width, int height, AAType aa,
                           const Matrix& viewMatrix, const Matrix& uvMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  bool hasUVPerspective() const override {
    return uvMatrix.hasPerspective();
  }

  Attribute position;
  Attribute coverage;
  Attribute color;

  PMColor commonColor;
  int width = 1;
  int height = 1;
  AAType aa = AAType::None;
  Matrix viewMatrix = {};
  Matrix uvMatrix = {};
};
}  // namespace tgfx
