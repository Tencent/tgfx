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

#include "GeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {
class ShapeInstancedGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<ShapeInstancedGeometryProcessor> Make(BlockAllocator* allocator,
                                                            PMColor color, int width, int height,
                                                            AAType aa, bool hasColors,
                                                            bool hasShader, const Matrix& uvMatrix,
                                                            const Matrix& stateMatrix);

  std::string name() const override {
    return "ShapeInstancedGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  ShapeInstancedGeometryProcessor(PMColor color, int width, int height, AAType aa, bool hasColors,
                                  bool hasShader, const Matrix& uvMatrix,
                                  const Matrix& stateMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute position = {};
  Attribute coverage = {};
  Attribute matrixCol0 = {};
  Attribute matrixCol1 = {};
  Attribute matrixCol2 = {};
  Attribute instanceColor = {};

  PMColor color = {};
  int width = 1;
  int height = 1;
  AAType aa = AAType::None;
  bool hasColors = false;
  bool hasShader = false;
  Matrix uvMatrix = {};
  Matrix stateMatrix = {};
};
}  // namespace tgfx
