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

#include "QuadPerEdgeAA3DGeometryProcessor.h"

namespace tgfx {

QuadPerEdgeAA3DGeometryProcessor::QuadPerEdgeAA3DGeometryProcessor(AAType aa)
    : GeometryProcessor(ClassID()), aa(aa) {
  position = {"aPosition", VertexFormat::Float2};
  if (aa == AAType::Coverage) {
    coverage = {"inCoverage", VertexFormat::Float};
  }
  setVertexAttributes(&position, 2);
}

void QuadPerEdgeAA3DGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = (aa == AAType::Coverage ? 1 : 0);
  // Distinguish from DefaultGeometryProcessor
  flags |= static_cast<uint32_t>(1) << 31;
  bytesKey->write(flags);
}

}  // namespace tgfx
