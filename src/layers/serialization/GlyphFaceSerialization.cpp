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

#include "GlyphFaceSerialization.h"

namespace tgfx {

std::shared_ptr<Data> GlyphFaceSerialization::serializeGlyphFace(GlyphFace* glyphFace) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  serializeGlyphFaceImpl(fbb, glyphFace);
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  ;
}
void GlyphFaceSerialization::serializeGlyphFaceImpl(flexbuffers::Builder& fbb,
                                                    GlyphFace* glyphFace) {
  SerializeUtils::setFlexBufferMap(fbb, "HasColor", glyphFace->hasColor());
  SerializeUtils::setFlexBufferMap(fbb, "HasOutlines", glyphFace->hasOutlines());
}
}  // namespace tgfx
#endif