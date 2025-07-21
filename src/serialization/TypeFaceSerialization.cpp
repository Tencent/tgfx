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
#ifdef TGFX_USE_INSPECTOR

#include "TypeFaceSerialization.h"

namespace tgfx {

std::shared_ptr<Data> TypeFaceSerialization::Serialize(const Typeface* typeface) {
  DEBUG_ASSERT(typeface != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeTypeFaceImpl(fbb, typeface);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void TypeFaceSerialization::SerializeTypeFaceImpl(flexbuffers::Builder& fbb,
                                                  const Typeface* typeface) {
  SerializeUtils::SetFlexBufferMap(fbb, "uniqueID", typeface->uniqueID());
  SerializeUtils::SetFlexBufferMap(fbb, "fontFamily", typeface->fontFamily());
  SerializeUtils::SetFlexBufferMap(fbb, "fontStyle", typeface->fontStyle());
  SerializeUtils::SetFlexBufferMap(fbb, "glyphsCount",
                                   static_cast<uint32_t>(typeface->glyphsCount()));
  SerializeUtils::SetFlexBufferMap(fbb, "unitsPerEm", typeface->unitsPerEm());
  SerializeUtils::SetFlexBufferMap(fbb, "hasColor", typeface->hasColor());
  SerializeUtils::SetFlexBufferMap(fbb, "hasOutlines", typeface->hasOutlines());
}
}  // namespace tgfx
#endif