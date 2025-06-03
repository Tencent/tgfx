/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#ifdef TGFX_USE_INSPECTOR

#include "ShapePaintSerialization.h"

namespace tgfx {

std::shared_ptr<Data> ShapePaintSerialization::Serialize(const ShapePaint* shapePaint,
                                                         SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap) {
  DEBUG_ASSERT(shapePaint != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
  SerializeShapePaintImpl(fbb, shapePaint, map, rosMap);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ShapePaintSerialization::SerializeShapePaintImpl(flexbuffers::Builder& fbb,
                                                      const ShapePaint* shapePaint,
                                                      SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap) {
  auto shaderID = SerializeUtils::GetObjID();
  auto shader = shapePaint->shader;
  SerializeUtils::SetFlexBufferMap(fbb, "shader", reinterpret_cast<uint64_t>(shader.get()), true,
                                   shader != nullptr, shaderID);
  SerializeUtils::FillComplexObjSerMap(shader, shaderID, map, rosMap);

  SerializeUtils::SetFlexBufferMap(fbb, "alpha", shapePaint->alpha);
  SerializeUtils::SetFlexBufferMap(fbb, "blendMode",
                                   SerializeUtils::BlendModeToString(shapePaint->blendMode));
}
}  // namespace tgfx
#endif