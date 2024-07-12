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

#pragma once

#include <memory>
#include <napi/native_api.h>
#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * OHOSBitmap provides a utility to access an OpenHarmony PixelMap object.
 */
class OHOSPixelMap {
 public:
  /**
   * Returns an ImageInfo describing the width, height, color type, alpha type, and row bytes of the
   * specified OpenHarmony PixelMap object. Only AlphaType::Opaque or AlphaType::Premultiplied PixelMap is supported.
   */
  static ImageInfo GetInfo(napi_env env, napi_value value);

  /**
   * Copy PixelMap into an Image. Only AlphaType::Opaque or AlphaType::Premultiplied PixelMap is supported.
   */
  static std::shared_ptr<Image> CopyImage(napi_env env, napi_value value);
};
}  // namespace tgfx