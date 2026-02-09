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

#include "MeshGeometryProcessor.h"

namespace tgfx {

MeshGeometryProcessor::MeshGeometryProcessor(bool hasTexCoords, bool hasColors, bool hasCoverage,
                                             PMColor color, const Matrix& viewMatrix)
    : GeometryProcessor(ClassID()), hasTexCoords(hasTexCoords), hasColors(hasColors),
      hasCoverage(hasCoverage), commonColor(color), viewMatrix(viewMatrix) {
  position = {"aPosition", VertexFormat::Float2};
  if (hasTexCoords) {
    texCoord = {"aTexCoord", VertexFormat::Float2};
  }
  if (hasColors) {
    this->color = {"aColor", VertexFormat::UByte4Normalized};
  }
  if (hasCoverage) {
    coverage = {"aCoverage", VertexFormat::Float};
  }
  setVertexAttributes(&position, 4);
}

void MeshGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = 0;
  if (hasTexCoords) {
    flags |= 1;
  }
  if (hasColors) {
    flags |= 2;
  }
  if (hasCoverage) {
    flags |= 4;
  }
  bytesKey->write(flags);
}

}  // namespace tgfx
