/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include <napi/native_api.h>
#include <memory>
#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * OHOSBitmap provides a utility to access an OpenHarmony PixelMap object.
 */
class OHOSPixelMap {
 public:
  /**
   * Returns an ImageInfo describing the width, height, color type, alpha type, and row bytes of the
   * specified OpenHarmony PixelMap object. Only Premultiplied PixelMap is supported.
   */
  static ImageInfo GetInfo(napi_env env, napi_value value);

  /**
   * Copy PixelMap into a Bitmap. Only Premultiplied PixelMap is supported.
   */
  static Bitmap CopyBitmap(napi_env env, napi_value value);
};
}  // namespace tgfx
