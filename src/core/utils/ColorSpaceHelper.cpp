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

#include "ColorSpaceHelper.h"

namespace tgfx {
static constexpr int32_t STANDARD_ADOBE_RGB = 0x000b0000;
static constexpr int32_t STANDARD_BT2020 = 0x00060000;
static constexpr int32_t STANDARD_BT2020_CONSTANT_LUMINANCE = 0x00070000;
static constexpr int32_t STANDARD_BT470M = 0x00080000;
static constexpr int32_t STANDARD_BT601_525 = 0x00040000;
static constexpr int32_t STANDARD_BT601_525_UNADJUSTED = 0x00050000;
static constexpr int32_t STANDARD_BT601_625 = 0x00020000;
static constexpr int32_t STANDARD_BT601_625_UNADJUSTED = 0x00030000;
static constexpr int32_t STANDARD_BT709 = 0x00010000;
static constexpr int32_t STANDARD_DCI_P3 = 0x000a0000;
static constexpr int32_t STANDARD_FILM = 0x00090000;
static constexpr int32_t TRANSFER_GAMMA2_2 = 0x01000000;
static constexpr int32_t TRANSFER_GAMMA2_6 = 0x01400000;
static constexpr int32_t TRANSFER_GAMMA2_8 = 0x01800000;
static constexpr int32_t TRANSFER_LINEAR = 0x00400000;
static constexpr int32_t TRANSFER_SRGB = 0x00800000;

std::shared_ptr<ColorSpace> MakeColorSpaceFromYUVColorSpace(YUVColorSpace yuvColorSpace) {
  ColorMatrix33 matrix{};
  switch (yuvColorSpace) {
    case YUVColorSpace::BT601_FULL:
    case YUVColorSpace::BT601_LIMITED:
    case YUVColorSpace::JPEG_FULL:
      NamedPrimaries::Rec601.toXYZD50(&matrix);
      return ColorSpace::MakeRGB(NamedTransferFunction::Rec601, matrix);
    case YUVColorSpace::BT709_FULL:
    case YUVColorSpace::BT709_LIMITED:
      NamedPrimaries::Rec709.toXYZD50(&matrix);
      return ColorSpace::MakeRGB(NamedTransferFunction::Rec709, matrix);
    case YUVColorSpace::BT2020_FULL:
    case YUVColorSpace::BT2020_LIMITED:
      return ColorSpace::MakeRGB(NamedTransferFunction::Rec2020, NamedGamut::Rec2020);
  }
}

std::shared_ptr<ColorSpace> AndroidDataSpaceToColorSpace(int standard, int transfer) {
  ColorMatrix33 gamut{};
  TransferFunction tf{};
  switch (standard) {
    case STANDARD_ADOBE_RGB:
      gamut = NamedGamut::AdobeRGB;
      break;
    case STANDARD_BT2020:
    case STANDARD_BT2020_CONSTANT_LUMINANCE:
      NamedPrimaries::Rec2020.toXYZD50(&gamut);
      break;
    case STANDARD_BT470M:
      NamedPrimaries::Rec470SystemM.toXYZD50(&gamut);
      break;
    case STANDARD_BT601_525:
    case STANDARD_BT601_525_UNADJUSTED:
      NamedPrimaries::Rec601.toXYZD50(&gamut);
      break;
    case STANDARD_BT601_625:
    case STANDARD_BT601_625_UNADJUSTED:
      NamedPrimaries::Rec470SystemBG.toXYZD50(&gamut);
      break;
    case STANDARD_BT709:
      NamedPrimaries::Rec709.toXYZD50(&gamut);
      break;
    case STANDARD_DCI_P3:
      NamedPrimaries::SMPTE_EG_432_1.toXYZD50(&gamut);
      break;
    case STANDARD_FILM:
      NamedPrimaries::GenericFilm.toXYZD50(&gamut);
      break;
    default:
      gamut = NamedGamut::SRGB;
  }
  switch (transfer) {
    case TRANSFER_GAMMA2_2:
      tf = NamedTransferFunction::TwoDotTwo;
      break;
    case TRANSFER_GAMMA2_6:
      tf = NamedTransferFunction::TwoDotTwo;
      tf.g = 2.6f;
      break;
    case TRANSFER_GAMMA2_8:
      tf = NamedTransferFunction::TwoDotTwo;
      tf.g = 2.8f;
      break;
    case TRANSFER_LINEAR:
      tf = NamedTransferFunction::Linear;
      break;
    case TRANSFER_SRGB:
      tf = NamedTransferFunction::SRGB;
      break;
    default:
      tf = NamedTransferFunction::SRGB;
  }
  return ColorSpace::MakeRGB(tf, gamut);
}

gfx::skcms_ICCProfile ToSkcmsICCProfile(std::shared_ptr<ColorSpace> colorSpace) {
  if (colorSpace) {
    gfx::skcms_ICCProfile profile;
    gfx::skcms_Init(&profile);
    auto transferFunction = colorSpace->transferFunction();
    gfx::skcms_SetTransferFunction(
        &profile, reinterpret_cast<gfx::skcms_TransferFunction*>(&transferFunction));
    ColorMatrix33 xyzd50{};
    colorSpace->toXYZD50(&xyzd50);
    skcms_SetXYZD50(&profile, reinterpret_cast<gfx::skcms_Matrix3x3*>(&xyzd50));
    return profile;
  }
  return {};
}

bool NeedConvertColorSpace(std::shared_ptr<ColorSpace> src, std::shared_ptr<ColorSpace> dst) {
  if (dst == nullptr) {
    return false;
  }
  if (src == nullptr) {
    src = ColorSpace::MakeSRGB();
  }
  return !ColorSpace::Equals(src.get(), dst.get());
}

void ConvertColorSpaceInPlace(int width, int height, ColorType colorType, AlphaType alphaType,
                              size_t rowBytes, std::shared_ptr<ColorSpace> srcCS,
                              std::shared_ptr<ColorSpace> dstCS, void* pixels) {
  if (!NeedConvertColorSpace(srcCS, dstCS)) {
    return;
  }
  auto srcImageInfo = ImageInfo::Make(width, height, colorType, alphaType, rowBytes, srcCS);
  auto dstImageInfo = srcImageInfo.makeColorSpace(dstCS);
  CopyPixels(srcImageInfo, pixels, dstImageInfo, pixels);
}
}  // namespace tgfx
