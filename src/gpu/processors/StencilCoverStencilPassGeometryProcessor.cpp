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

#include "StencilCoverStencilPassGeometryProcessor.h"
#include "core/utils/Log.h"

namespace tgfx {
StencilCoverStencilPassGeometryProcessor::StencilCoverStencilPassGeometryProcessor(
    const Matrix& viewMatrix)
    : GeometryProcessor(ClassID()), viewMatrix(viewMatrix) {
  position = {"aPosition", VertexFormat::Float2};
  klm = {"aKLM", VertexFormat::Float3};
  // setVertexAttributes treats the pair as Attribute[2]; verify the members really are
  // adjacent. Attribute is not standard-layout (it holds a std::string), so offsetof is not
  // usable here and the check has to run on a constructed instance.
  DEBUG_ASSERT(reinterpret_cast<const Attribute*>(&klm) ==
               reinterpret_cast<const Attribute*>(&position) + 1);
  setVertexAttributes(&position, 2);
}
}  // namespace tgfx
