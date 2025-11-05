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

#include "CopyPixels.h"
#include <unordered_map>
#include "ColorSpaceHelper.h"
#include "core/utils/Log.h"
#include "skcms.h"

namespace tgfx {

inline void* AddOffset(void* pixels, size_t offset) {
  return static_cast<uint8_t*>(pixels) + offset;
}

inline const void* AddOffset(const void* pixels, size_t offset) {
  return static_cast<const uint8_t*>(pixels) + offset;
}

static void CopyRectMemory(const void* src, size_t srcRB, void* dst, size_t dstRB,
                           size_t trimRowBytes, size_t rowCount, bool flipY) {
  if (!flipY && trimRowBytes == dstRB && trimRowBytes == srcRB) {
    memcpy(dst, src, trimRowBytes * rowCount);
    return;
  }
  auto srcRowOffset = static_cast<int64_t>(srcRB);
  if (flipY) {
    src = AddOffset(src, (rowCount - 1) * srcRB);
    srcRowOffset = -srcRowOffset;
  }
  for (size_t i = 0; i < rowCount; i++) {
    memcpy(dst, src, trimRowBytes);
    dst = AddOffset(dst, dstRB);
    src = static_cast<const uint8_t*>(src) + srcRowOffset;
  }
}

static const std::unordered_map<ColorType, gfx::skcms_PixelFormat> ColorMapper{
    {ColorType::RGBA_8888, gfx::skcms_PixelFormat::skcms_PixelFormat_RGBA_8888},
    {ColorType::BGRA_8888, gfx::skcms_PixelFormat::skcms_PixelFormat_BGRA_8888},
    {ColorType::ALPHA_8, gfx::skcms_PixelFormat::skcms_PixelFormat_A_8},
    {ColorType::RGB_565, gfx::skcms_PixelFormat::skcms_PixelFormat_BGR_565},
    {ColorType::Gray_8, gfx::skcms_PixelFormat::skcms_PixelFormat_G_8},
    {ColorType::RGBA_F16, gfx::skcms_PixelFormat::skcms_PixelFormat_RGBA_hhhh},
    {ColorType::RGBA_1010102, gfx::skcms_PixelFormat::skcms_PixelFormat_RGBA_1010102},
};

static const std::unordered_map<AlphaType, gfx::skcms_AlphaFormat> AlphaMapper{
    {AlphaType::Unpremultiplied, gfx::skcms_AlphaFormat::skcms_AlphaFormat_Unpremul},
    {AlphaType::Premultiplied, gfx::skcms_AlphaFormat::skcms_AlphaFormat_PremulAsEncoded},
    {AlphaType::Opaque, gfx::skcms_AlphaFormat::skcms_AlphaFormat_Opaque},
};

void CopyPixels(const ImageInfo& srcInfo, const void* srcPixels, const ImageInfo& dstInfo,
                void* dstPixels, bool flipY) {
  DEBUG_ASSERT(!srcInfo.isEmpty());
  DEBUG_ASSERT(srcInfo.width() == dstInfo.width() && srcInfo.height() == dstInfo.height());
  auto srcColorSpace = srcInfo.colorSpace();
  auto dstColorSpace = dstInfo.colorSpace();
  if (srcInfo.colorType() == dstInfo.colorType() && srcInfo.alphaType() == dstInfo.alphaType() &&
      ColorSpaceIsEqual(srcColorSpace, dstColorSpace)) {
    CopyRectMemory(srcPixels, srcInfo.rowBytes(), dstPixels, dstInfo.rowBytes(),
                   dstInfo.minRowBytes(), static_cast<size_t>(dstInfo.height()), flipY);
    return;
  }
  auto srcFormat = ColorMapper.at(srcInfo.colorType());
  auto srcAlpha = AlphaMapper.at(srcInfo.alphaType());
  auto dstFormat = ColorMapper.at(dstInfo.colorType());
  auto dstAlpha = AlphaMapper.at(dstInfo.alphaType());
  auto width = dstInfo.width();
  auto height = dstInfo.height();
  auto srcRowOffset = static_cast<int64_t>(srcInfo.rowBytes());
  if (flipY) {
    srcPixels = AddOffset(srcPixels, static_cast<size_t>(height - 1) * srcInfo.rowBytes());
    srcRowOffset = -srcRowOffset;
  }
  if (!srcColorSpace) {
    srcColorSpace = ColorSpace::MakeSRGB();
  }
  if (!dstColorSpace) {
    dstColorSpace = srcColorSpace;
  }
  auto srcProfile = ToSkcmsICCProfile(srcColorSpace);
  auto dstProfile = ToSkcmsICCProfile(dstColorSpace);
  for (int i = 0; i < height; i++) {
    gfx::skcms_Transform(srcPixels, srcFormat, srcAlpha, &srcProfile, dstPixels, dstFormat,
                         dstAlpha, &dstProfile, static_cast<size_t>(width));
    dstPixels = AddOffset(dstPixels, dstInfo.rowBytes());
    srcPixels = static_cast<const uint8_t*>(srcPixels) + srcRowOffset;
  }
}
}  // namespace tgfx
