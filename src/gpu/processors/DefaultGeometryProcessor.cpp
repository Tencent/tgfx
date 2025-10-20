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

#include "DefaultGeometryProcessor.h"

namespace tgfx {
DefaultGeometryProcessor::DefaultGeometryProcessor(Color color, int width, int height, AAType aa,
                                                   const Matrix& viewMatrix, const Matrix& uvMatrix,
                                                   std::shared_ptr<ColorSpace> dstColorSpace)
    : GeometryProcessor(ClassID()), color(color), width(width), height(height), aa(aa),
      viewMatrix(viewMatrix), uvMatrix(uvMatrix), dstColorSpace(std::move(dstColorSpace)) {
  position = {"aPosition", VertexFormat::Float2};
  if (aa == AAType::Coverage) {
    coverage = {"inCoverage", VertexFormat::Float};
  }
  setVertexAttributes(&position, 2);
}

void DefaultGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = aa == AAType::Coverage ? 1 : 0;
  bytesKey->write(flags);
}
}  // namespace tgfx
