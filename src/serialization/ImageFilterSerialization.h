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

#pragma once

#include <tgfx/core/Data.h>
#include "SerializationUtils.h"

namespace tgfx {
class ImageFilterSerialization {
 public:
  static std::shared_ptr<Data> Serialize(ImageFilter* imageFilter);

 private:
  static void serializeImageFilterImpl(flexbuffers::Builder& fbb, ImageFilter* imageFilter);

  static void serializeColorImageFilter(flexbuffers::Builder& fbb, ImageFilter* imageFilter);

  static void serializeBlurImageFilter(flexbuffers::Builder& fbb, ImageFilter* imageFilter);

  static void serializeComposeImageFilter(flexbuffers::Builder& fbb, ImageFilter* imageFilter);

  static void serializeDropShadowImageFilter(flexbuffers::Builder& fbb, ImageFilter* imageFilter);

  static void serializeInnerShadowImageFilter(flexbuffers::Builder& fbb, ImageFilter* imageFilter);

  static void serializeRuntimeImageFilter(flexbuffers::Builder& fbb, ImageFilter* imageFilter);
};
}  // namespace tgfx
