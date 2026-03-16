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

#include "ShapeInstancedGeometryProcessor.h"

namespace tgfx {
ShapeInstancedGeometryProcessor::ShapeInstancedGeometryProcessor(int width, int height, AAType aa,
                                                                 bool hasColors,
                                                                 const Matrix& uvMatrix,
                                                                 const Matrix& stateMatrix)
    : GeometryProcessor(ClassID()), width(width), height(height), aa(aa), hasColors(hasColors),
      uvMatrix(uvMatrix), stateMatrix(stateMatrix) {
  position = {"aPosition", VertexFormat::Float2};
  if (aa == AAType::Coverage) {
    coverage = {"inCoverage", VertexFormat::Float};
  }
  setVertexAttributes(&position, 2);

  offset = {"aOffset", VertexFormat::Float2};
  if (hasColors) {
    instanceColor = {"aColor", VertexFormat::UByte4Normalized};
  }
  setInstanceAttributes(&offset, 2);
}

void ShapeInstancedGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = 0;
  if (hasColors) flags |= 1;
  if (aa == AAType::Coverage) flags |= 2;
  bytesKey->write(flags);
}
}  // namespace tgfx
