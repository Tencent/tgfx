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

#include "tgfx/core/ColorSpace.h"
#include <skcms.h>
#include <cmath>
#include "tgfx/core/Checksum.h"
#include "utils/Log.h"

namespace tgfx {

namespace namedPrimaries {

bool GetCicp(CicpId primaries, ColorSpacePrimaries& colorSpacePrimaries) {
  // Rec. ITU-T H.273, Table 2.
  switch (primaries) {
    case CicpId::Rec709:
      colorSpacePrimaries = Rec709;
      return true;
    case CicpId::Rec470SystemM:
      colorSpacePrimaries = Rec470SystemM;
      return true;
    case CicpId::Rec470SystemBG:
      colorSpacePrimaries = Rec470SystemBG;
      return true;
    case CicpId::Rec601:
      colorSpacePrimaries = Rec601;
      return true;
    case CicpId::SMPTE_ST_240:
      colorSpacePrimaries = SMPTE_ST_240;
      return true;
    case CicpId::GenericFilm:
      colorSpacePrimaries = GenericFilm;
      return true;
    case CicpId::Rec2020:
      colorSpacePrimaries = Rec2020;
      return true;
    case CicpId::SMPTE_ST_428_1:
      colorSpacePrimaries = SMPTE_ST_428_1;
      return true;
    case CicpId::SMPTE_RP_431_2:
      colorSpacePrimaries = SMPTE_RP_431_2;
      return true;
    case CicpId::SMPTE_EG_432_1:
      colorSpacePrimaries = SMPTE_EG_432_1;
      return true;
    case CicpId::ITU_T_H273_Value22:
      colorSpacePrimaries = ITU_T_H273_Value22;
      return true;
    default:
      // Reserved or unimplemented.
      break;
  }
  return false;
}

}  // namespace namedPrimaries

namespace namedTransferFn {

bool GetCicp(CicpId transferCharacteristics, TransferFunction& trfn) {
  // Rec. ITU-T H.273, Table 3.
  switch (transferCharacteristics) {
    case CicpId::Rec709:
      trfn = Rec709;
      return true;
    case CicpId::Rec470SystemM:
      trfn = Rec470SystemM;
      return true;
    case CicpId::Rec470SystemBG:
      trfn = Rec470SystemBG;
      return true;
    case CicpId::Rec601:
      trfn = Rec601;
      return true;
    case CicpId::SMPTE_ST_240:
      trfn = SMPTE_ST_240;
      return true;
    case CicpId::Linear:
      trfn = Linear;
      return true;
    case CicpId::IEC61966_2_4:
      trfn = IEC61966_2_4;
      return true;
    case CicpId::IEC61966_2_1:
      trfn = IEC61966_2_1;
      return true;
    case CicpId::Rec2020_10bit:
      trfn = Rec2020_10bit;
      return true;
    case CicpId::Rec2020_12bit:
      trfn = Rec2020_12bit;
      return true;
    case CicpId::PQ:
      trfn = PQ;
      return true;
    case CicpId::SMPTE_ST_428_1:
      trfn = SMPTE_ST_428_1;
      return true;
    case CicpId::HLG:
      trfn = HLG;
      return true;
    default:
      // Reserved or unimplemented.
      break;
  }
  return false;
}

}  // namespace namedTransferFn

static bool ColorSpaceAlmostEqual(float a, float b) {
  return fabsf(a - b) < 0.01f;
}

static bool XYZAlmostEqual(const Matrix3x3& mA, const Matrix3x3& mB) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      if (!ColorSpaceAlmostEqual(mA.vals[r][c], mB.vals[r][c])) {
        return false;
      }
    }
  }
  return true;
}

// Let's use a stricter version for transfer functions.  Worst case, these are encoded
// in ICC format, which offers 16-bits of fractional precision.
static bool TransferFnAlmostEqual(float a, float b) {
  return fabsf(a - b) < 0.001f;
}

static bool IsAlmostSRGB(const TransferFunction& coeffs) {
  return TransferFnAlmostEqual(namedTransferFn::SRGB.a, coeffs.a) &&
         TransferFnAlmostEqual(namedTransferFn::SRGB.b, coeffs.b) &&
         TransferFnAlmostEqual(namedTransferFn::SRGB.c, coeffs.c) &&
         TransferFnAlmostEqual(namedTransferFn::SRGB.d, coeffs.d) &&
         TransferFnAlmostEqual(namedTransferFn::SRGB.e, coeffs.e) &&
         TransferFnAlmostEqual(namedTransferFn::SRGB.f, coeffs.f) &&
         TransferFnAlmostEqual(namedTransferFn::SRGB.g, coeffs.g);
}

static bool IsAlmost2dot2(const TransferFunction& coeffs) {
  return TransferFnAlmostEqual(1.0f, coeffs.a) && TransferFnAlmostEqual(0.0f, coeffs.b) &&
         TransferFnAlmostEqual(0.0f, coeffs.e) && TransferFnAlmostEqual(2.2f, coeffs.g) &&
         coeffs.d <= 0.0f;
}

static bool IsAlmostLinear(const TransferFunction& coeffs) {
  // OutputVal = InputVal ^ 1.0f
  const bool linearExp = TransferFnAlmostEqual(1.0f, coeffs.a) &&
                         TransferFnAlmostEqual(0.0f, coeffs.b) &&
                         TransferFnAlmostEqual(0.0f, coeffs.e) &&
                         TransferFnAlmostEqual(1.0f, coeffs.g) && coeffs.d <= 0.0f;

  // OutputVal = 1.0f * InputVal
  const bool linearFn = TransferFnAlmostEqual(1.0f, coeffs.c) &&
                        TransferFnAlmostEqual(0.0f, coeffs.f) && coeffs.d >= 1.0f;

  return linearExp || linearFn;
}

bool ColorSpacePrimaries::toXYZD50(Matrix3x3* toXYZD50) const {
  return gfx::skcms_PrimariesToXYZD50(fRX, fRY, fGX, fGY, fBX, fBY, fWX, fWY,
                                      reinterpret_cast<gfx::skcms_Matrix3x3*>(toXYZD50));
}

std::shared_ptr<ColorSpace> ColorSpace::MakeSRGB() {
  static std::shared_ptr<ColorSpace> cs =
      std::shared_ptr<ColorSpace>(new ColorSpace(namedTransferFn::SRGB, namedGamut::SRGB));
  return cs;
}

std::shared_ptr<ColorSpace> ColorSpace::MakeSRGBLinear() {
  static std::shared_ptr<ColorSpace> cs =
      std::shared_ptr<ColorSpace>(new ColorSpace(namedTransferFn::Linear, namedGamut::SRGB));
  return cs;
}

std::shared_ptr<ColorSpace> ColorSpace::MakeRGB(const TransferFunction& transferFn,
                                                const Matrix3x3& toXYZ) {
  if (gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
          &transferFn)) == gfx::skcms_TFType_Invalid) {
    return nullptr;
  }

  const TransferFunction* tf = &transferFn;

  if (IsAlmostSRGB(transferFn)) {
    if (XYZAlmostEqual(toXYZ, namedGamut::SRGB)) {
      return ColorSpace::MakeSRGB();
    }
    tf = &namedTransferFn::SRGB;
  } else if (IsAlmost2dot2(transferFn)) {
    tf = &namedTransferFn::_2Dot2;
  } else if (IsAlmostLinear(transferFn)) {
    if (XYZAlmostEqual(toXYZ, namedGamut::SRGB)) {
      return ColorSpace::MakeSRGBLinear();
    }
    tf = &namedTransferFn::Linear;
  }

  return std::shared_ptr<ColorSpace>(new ColorSpace(*tf, toXYZ));
}

std::shared_ptr<ColorSpace> ColorSpace::MakeCICP(namedPrimaries::CicpId colorPrimaries,
                                                 namedTransferFn::CicpId transferCharacteristics) {
  TransferFunction trfn;
  if (!namedTransferFn::GetCicp(transferCharacteristics, trfn)) {
    return nullptr;
  }

  ColorSpacePrimaries primaries;
  if (!namedPrimaries::GetCicp(colorPrimaries, primaries)) {
    return nullptr;
  }

  Matrix3x3 primariesMatrix;
  if (!primaries.toXYZD50(&primariesMatrix)) {
    return nullptr;
  }

  return ColorSpace::MakeRGB(trfn, primariesMatrix);
}

std::shared_ptr<ColorSpace> ColorSpace::MakeFromICC(const void* data, size_t size) {
  gfx::skcms_ICCProfile profile;
  if (!gfx::skcms_Parse(data, size, &profile)) {
    return nullptr;
  }
  if (!profile.has_toXYZD50 || !profile.has_trc) {
    return nullptr;
  }
  if (skcms_ApproximatelyEqualProfiles(&profile, gfx::skcms_sRGB_profile())) {
    return ColorSpace::MakeSRGB();
  }

  gfx::skcms_Matrix3x3 inv;
  if (!skcms_Matrix3x3_invert(&profile.toXYZD50, &inv)) {
    return nullptr;
  }

  const gfx::skcms_Curve* trc = profile.trc;

  if (trc[0].table_entries != 0 || trc[1].table_entries != 0 || trc[2].table_entries != 0 ||
      0 != memcmp(&trc[0].parametric, &trc[1].parametric, sizeof(trc[0].parametric)) ||
      0 != memcmp(&trc[0].parametric, &trc[2].parametric, sizeof(trc[0].parametric))) {
    if (gfx::skcms_TRCs_AreApproximateInverse(
            reinterpret_cast<const gfx::skcms_ICCProfile*>(&profile),
            gfx::skcms_sRGB_Inverse_TransferFunction())) {
      return ColorSpace::MakeRGB(namedTransferFn::SRGB,
                                 *reinterpret_cast<Matrix3x3*>(&profile.toXYZD50));
    }
    return nullptr;
  }

  return ColorSpace::MakeRGB(*reinterpret_cast<TransferFunction*>(&profile.trc[0].parametric),
                             *reinterpret_cast<Matrix3x3*>(&profile.toXYZD50));
}

bool ColorSpace::gammaCloseToSRGB() const {
  // Nearly-equal transfer functions were snapped at construction time, so just do an exact test
  return memcmp(&fTransferFn, &namedTransferFn::SRGB, 7 * sizeof(float)) == 0;
}

bool ColorSpace::gammaIsLinear() const {
  // Nearly-equal transfer functions were snapped at construction time, so just do an exact test
  return memcmp(&fTransferFn, &namedTransferFn::Linear, 7 * sizeof(float)) == 0;
}

bool ColorSpace::isNumericalTransferFn(TransferFunction* fn) const {
  this->transferFn(fn);
  return gfx::skcms_TransferFunction_getType(reinterpret_cast<gfx::skcms_TransferFunction*>(fn)) ==
         gfx::skcms_TFType_sRGBish;
}

bool ColorSpace::toXYZD50(Matrix3x3* toXYZD50) const {
  *toXYZD50 = fToXYZD50;
  return true;
}

std::shared_ptr<ColorSpace> ColorSpace::makeLinearGamma() const {
  if (this->gammaIsLinear()) {
    return const_cast<ColorSpace*>(this)->shared_from_this();
  }
  return ColorSpace::MakeRGB(namedTransferFn::Linear, fToXYZD50);
}

std::shared_ptr<ColorSpace> ColorSpace::makeSRGBGamma() const {
  if (this->gammaCloseToSRGB()) {
    return const_cast<ColorSpace*>(this)->shared_from_this();
  }
  return ColorSpace::MakeRGB(namedTransferFn::SRGB, fToXYZD50);
}

std::shared_ptr<ColorSpace> ColorSpace::makeColorSpin() const {
  gfx::skcms_Matrix3x3 spin = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}};
  gfx::skcms_Matrix3x3 spun =
      gfx::skcms_Matrix3x3_concat(reinterpret_cast<const gfx::skcms_Matrix3x3*>(&fToXYZD50), &spin);
  return std::shared_ptr<ColorSpace>(
      new ColorSpace(fTransferFn, *reinterpret_cast<Matrix3x3*>(&spun)));
}

bool ColorSpace::isSRGB() const {
  return this == MakeSRGB().get();
}

std::shared_ptr<Data> ColorSpace::serialize() const {
  size_t length = this->writeToMemory(nullptr);
  void* memory = malloc(length);
  this->writeToMemory(memory);
  return Data::MakeAdopted(memory, length, Data::FreeProc);
}

enum Version {
  k0_Version,  // Initial (deprecated) version, no longer supported
  k1_Version,  // Simple header (version tag) + 16 floats

  kCurrent_Version = k1_Version,
};

struct ColorSpaceHeader {
  uint8_t fVersion = kCurrent_Version;

  // Other fields were only used by k0_Version. Could be re-purposed in future versions.
  uint8_t fReserved0 = 0;
  uint8_t fReserved1 = 0;
  uint8_t fReserved2 = 0;
};

size_t ColorSpace::writeToMemory(void* memory) const {
  auto temp = static_cast<uint8_t*>(memory);
  if (temp) {
    *((ColorSpaceHeader*)temp) = ColorSpaceHeader();
    temp += sizeof(ColorSpaceHeader);

    memcpy(temp, &fTransferFn, 7 * sizeof(float));
    temp += 7 * sizeof(float);

    memcpy(temp, &fToXYZD50, 9 * sizeof(float));
  }

  return sizeof(ColorSpaceHeader) + 16 * sizeof(float);
}

std::shared_ptr<ColorSpace> ColorSpace::Deserialize(const void* data, size_t length) {
  if (length < sizeof(ColorSpaceHeader)) {
    return nullptr;
  }
  auto temp = static_cast<const uint8_t*>(data);
  ColorSpaceHeader header = *((const ColorSpaceHeader*)temp);
  temp += sizeof(ColorSpaceHeader);
  length -= sizeof(ColorSpaceHeader);
  if (header.fVersion != k1_Version) {
    return nullptr;
  }

  if (length < 16 * sizeof(float)) {
    return nullptr;
  }

  TransferFunction transferFn;
  memcpy(&transferFn, temp, 7 * sizeof(float));
  temp += 7 * sizeof(float);

  Matrix3x3 toXYZ;
  memcpy(&toXYZ, temp, 9 * sizeof(float));
  return ColorSpace::MakeRGB(transferFn, toXYZ);
}

bool ColorSpace::Equals(const ColorSpace* x, const ColorSpace* y) {
  if (x == y) {
    return true;
  }
  if (!x || !y) {
    return false;
  }
  if (x->hash() == y->hash()) {
    return true;
  }
  return false;
}

void ColorSpace::transferFn(TransferFunction* fn) const {
  *fn = fTransferFn;
}

void ColorSpace::invTransferFn(TransferFunction* fn) const {
  computeLazyDstFields();
  *fn = fInvTransferFn;
}

void ColorSpace::gamutTransformTo(const ColorSpace* dst, Matrix3x3* srcToDst) const {
  dst->computeLazyDstFields();
  auto result =
      gfx::skcms_Matrix3x3_concat(reinterpret_cast<const gfx::skcms_Matrix3x3*>(&dst->fFromXYZD50),
                                  reinterpret_cast<const gfx::skcms_Matrix3x3*>(&fToXYZD50));
  *srcToDst = *reinterpret_cast<Matrix3x3*>(&result);
}

ColorSpace::ColorSpace(const TransferFunction& transferFn, const Matrix3x3& toXYZ)
    : fTransferFn(transferFn), fToXYZD50(toXYZ) {
  fTransferFnHash = checksum::Hash32(&fTransferFn, 7 * sizeof(float));
  fToXYZD50Hash = checksum::Hash32(&fToXYZD50, 9 * sizeof(float));
}

void ColorSpace::computeLazyDstFields() const {
  if (!isLazyDstFieldsResolved) {

    // Invert 3x3 gamut, defaulting to sRGB if we can't.
    if (!gfx::skcms_Matrix3x3_invert(reinterpret_cast<const gfx::skcms_Matrix3x3*>(&fToXYZD50),
                                     reinterpret_cast<gfx::skcms_Matrix3x3*>(&fFromXYZD50))) {
      ASSERT(gfx::skcms_Matrix3x3_invert(&gfx::skcms_sRGB_profile()->toXYZD50,
                                         reinterpret_cast<gfx::skcms_Matrix3x3*>(&fFromXYZD50)))
    }

    // Invert transfer function, defaulting to sRGB if we can't.
    if (!gfx::skcms_TransferFunction_invert(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&fTransferFn),
            reinterpret_cast<gfx::skcms_TransferFunction*>(&fInvTransferFn))) {
      fInvTransferFn =
          *reinterpret_cast<const TransferFunction*>(gfx::skcms_sRGB_Inverse_TransferFunction());
    }

    isLazyDstFieldsResolved = true;
  }
}

}  // namespace tgfx
