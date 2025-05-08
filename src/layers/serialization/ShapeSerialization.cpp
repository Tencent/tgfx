/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#ifdef TGFX_ENABLE_LAYER_INSPECTOR

#include "ShapeSerialization.h"

namespace tgfx {

std::shared_ptr<Data> ShapeSerialization::serializeShape(Shape* shape) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  serializeShapeImpl(fbb, shape);
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void ShapeSerialization::serializeShapeImpl(flexbuffers::Builder& fbb, Shape* shape) {
  SerializeUtils::setFlexBufferMap(fbb, "Type", SerializeUtils::shapeTypeToString(shape->type()));
  SerializeUtils::setFlexBufferMap(fbb, "IsSimplePath", shape->isSimplePath());
  SerializeUtils::setFlexBufferMap(fbb, "IsInverseFillType", shape->isInverseFillType());
}
}  // namespace tgfx
#endif