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

#include <cstddef>
#include "tgfx/core/AlphaType.h"
#include "tgfx/core/ColorType.h"
#include "tgfx/core/Orientation.h"

namespace tgfx {

class OHOSImageInfo {
 public:
  /**
   * Convert the orientation attribute returned from Openharmony to the orientation of TGFX
   */
  static Orientation ToTGFXOrientation(const char* value, size_t size);

  /**
   * Convert the pixelFormat attribute returned from Openharmony to the colorType attribute of TGFX. If the corresponding
   * attribute is not defined in TGFX, it will return tgfx::ColorType::Unknown.
   */
  static ColorType ToTGFXColorType(int ohPixelFormat);

  /**
   * Convert the alphaType attribute returned from Openharmony to the alphaType attribute of TGFX.
   */
  static AlphaType ToTGFXAlphaType(int ohAlphaType);

  /**
   * Convert the pixelFormat attribute returned from TGFX to the colorType attribute of Openharmony. If the corresponding
   * attribute is not defined in HarmonyOS, it will return PIXEL_FORMAT_UNKNOWN.
   * */
  static int ToOHPixelFormat(ColorType colorType);
};
}  // namespace tgfx
