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

#pragma once
#include "tgfx/core/ImageInfo.h"

namespace tgfx {

// PixelLayout specifies:
//  number of channels
//  order of channels
//  whether color is premultiplied by alpha
enum class PixelLayout {
  CHANNEL_1  = 1,
  CHANNEL_2  = 2,
  // 3-chan, with order specified (for channel flipping)
  RGB        = 3,
  BGR        = 0,
  CHANNEL_4  = 5,

  // alpha formats, where alpha is NOT premultiplied into color channels
  RGBA       = 4,
  BGRA       = 6,
  ARGB       = 7,
  ABGR       = 8,
  RA         = 9,
  AR         = 10,

  // alpha formats, where alpha is premultiplied into color channels
  RGBA_PM    = 11,
  BGRA_PM    = 12,
  ARGB_PM    = 13,
  ABGR_PM    = 14,
  RA_PM      = 15,
  AR_PM      = 16,

  // alpha formats, where NO alpha weighting is applied at all! these are just synonyms for the _PM
  // flags (which also do no alpha weighting). These names just make it more clear for some folks.
  RGBA_NO_AW = 11,
  BGRA_NO_AW = 12,
  ARGB_NO_AW = 13,
  ABGR_NO_AW = 14,
  RA_NO_AW   = 15,
  AR_NO_AW   = 16
};

enum class DataType {
  UINT8            = 0,
  UINT8_SRGB       = 1,
  // alpha channel, when present, should also be SRGB (this is very unusual)
  UINT8_SRGB_ALPHA = 2,
  UINT16           = 3,
  FLOAT            = 4,
  HALF_FLOAT       = 5
};

bool ImageResize(const void* inputPixels, const ImageInfo& inputInfo, void* outputPixels, const ImageInfo& dstImageInfo);
}  // namespace tgfx
