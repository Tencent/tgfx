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

#include "FontSerialization.h"

namespace tgfx {

std::shared_ptr<Data> FontSerialization::Serialize(Font* font) {
  DEBUG_ASSERT(font != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeFontImpl(fbb, font);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void FontSerialization::SerializeFontImpl(flexbuffers::Builder& fbb, Font* font) {
  auto typeFace = font->getTypeface();
  SerializeUtils::SetFlexBufferMap(fbb, "typeFace", reinterpret_cast<uint64_t>(typeFace.get()),
                                   true, typeFace != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "hasColor", font->hasColor());
  SerializeUtils::SetFlexBufferMap(fbb, "hasOutlines", font->hasOutlines());
  SerializeUtils::SetFlexBufferMap(fbb, "size", font->getSize());
  SerializeUtils::SetFlexBufferMap(fbb, "isFauxBold", font->isFauxBold());
  SerializeUtils::SetFlexBufferMap(fbb, "isFauxItalic", font->isFauxItalic());
  SerializeUtils::SetFlexBufferMap(fbb, "metrics", "", false, true);
}
}  // namespace tgfx
#endif