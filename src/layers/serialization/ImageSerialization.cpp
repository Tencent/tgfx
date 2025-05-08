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

#include "ImageSerialization.h"

namespace tgfx {

std::shared_ptr<Data> ImageSerialization::serializeImage(Image* image) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  serializeImageImpl(fbb, image);
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ImageSerialization::serializeImageImpl(flexbuffers::Builder& fbb, Image* image) {
  SerializeUtils::setFlexBufferMap(fbb, "Type", SerializeUtils::imageTypeToString(image->type()));
  SerializeUtils::setFlexBufferMap(fbb, "Width", image->width());
  SerializeUtils::setFlexBufferMap(fbb, "Height", image->height());
  SerializeUtils::setFlexBufferMap(fbb, "IsAlphaOnly", image->isAlphaOnly());
  SerializeUtils::setFlexBufferMap(fbb, "HasMipmaps", image->hasMipmaps());
  SerializeUtils::setFlexBufferMap(fbb, "IsFullyDecoded", image->isFullyDecoded());
  SerializeUtils::setFlexBufferMap(fbb, "IsTextureBacked", image->isTextureBacked());
}
}  // namespace tgfx
#endif