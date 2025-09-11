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
#include "core/Matrix3D.h"
#include "gpu/AAType.h"

namespace tgfx {

class QuadPerEdgeAA3DGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<QuadPerEdgeAA3DGeometryProcessor> Make(BlockBuffer* buffer, AAType aa,
                                                             const Matrix3D& transformMatrix,
                                                             const Matrix& adjustMatrix);

  std::string name() const override {
    return "QuadPerEdgeAA3DGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit QuadPerEdgeAA3DGeometryProcessor(AAType aa, const Matrix3D& transfromMatrix,
                                            const Matrix& adjustMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Attribute position;

  Attribute coverage;

  AAType aa = AAType::None;

  Matrix3D transfromMatrix;

  Matrix adjustMatrix;
};

}  // namespace tgfx