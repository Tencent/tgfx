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

#include "ImageSerialization.h"

namespace tgfx {

std::shared_ptr<Data> ImageSerialization::Serialize(Image* image) {
  DEBUG_ASSERT(image != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeImageImpl(fbb, image);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ImageSerialization::SerializeImageImpl(flexbuffers::Builder& fbb, Image* image) {
  SerializeUtils::SetFlexBufferMap(fbb, "Type",
                                   SerializeUtils::ImageTypeToString(Types::Get(image)));
  SerializeUtils::SetFlexBufferMap(fbb, "Width", image->width());
  SerializeUtils::SetFlexBufferMap(fbb, "Height", image->height());
  SerializeUtils::SetFlexBufferMap(fbb, "IsAlphaOnly", image->isAlphaOnly());
  SerializeUtils::SetFlexBufferMap(fbb, "HasMipmaps", image->hasMipmaps());
  SerializeUtils::SetFlexBufferMap(fbb, "IsFullyDecoded", image->isFullyDecoded());
  SerializeUtils::SetFlexBufferMap(fbb, "IsTextureBacked", image->isTextureBacked());
}
}  // namespace tgfx
#endif