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

#include "HairlineLineGeometryProcessor.h"
#include "tgfx/core/Color.h"

namespace tgfx {

HairlineLineGeometryProcessor::HairlineLineGeometryProcessor(const PMColor& color,
                                                             const Matrix& viewMatrix,
                                                             std::optional<Matrix> uvMatrix,
                                                             uint8_t coverage)
    : GeometryProcessor(ClassID()), color(color), viewMatrix(viewMatrix), uvMatrix(uvMatrix),
      coverage(coverage) {
  position = {"aPosition", VertexFormat::Float2};
  edgeDistance = {"aEdgeDistance", VertexFormat::Float};
  setVertexAttributes(&position, 2);
}

void HairlineLineGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(coverage != 0xFF ? 1 : 0));
}

}  // namespace tgfx
