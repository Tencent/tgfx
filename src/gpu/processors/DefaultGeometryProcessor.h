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
  static PlacementPtr<DefaultGeometryProcessor> Make(BlockAllocator* allocator, Color color,
                                                     int width, int height, AAType aa,
                                                     const Matrix& viewMatrix,
                                                     const Matrix& uvMatrix);

  std::string name() const override {
    return "DefaultGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  DefaultGeometryProcessor(Color color, int width, int height, AAType aa, const Matrix& viewMatrix,
                           const Matrix& uvMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute position;
  Attribute coverage;

  Color color;
  int width = 1;
  int height = 1;
  AAType aa = AAType::None;
  Matrix viewMatrix = {};
  Matrix uvMatrix = {};
};
}  // namespace tgfx
