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

#include "TextBlobSerialization.h"

namespace tgfx {

std::shared_ptr<Data> TextBlobSerialization::serializeTextBlob(TextBlob* textBlob) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  serializeTextBlobImpl(fbb, textBlob);
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void TextBlobSerialization::serializeTextBlobImpl(flexbuffers::Builder& fbb, TextBlob* textBlob) {
  auto glyphRunListsSize = static_cast<unsigned int>(textBlob->glyphRunLists.size());
  SerializeUtils::setFlexBufferMap(fbb, "GlyphRunLists", glyphRunListsSize, false,
                                   glyphRunListsSize);
}
}  // namespace tgfx
#endif