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
#include "tgfx/core/ColorSpaceXformSteps.h"
#include <skcms.h>
#include <cstring>
#include "tgfx/core/AlphaType.h"
#include "tgfx/core/ColorSpace.h"
#include "utils/Log.h"

namespace tgfx {

// Compute the Y vector for the HLG OOTF in the primaries of a specified color space. The value
// is specified in Rec2020 primaries in ITU-R BT.2100.
static void SetOotfY(const ColorSpace* cs, float* Y) {
  Matrix3x3 m;
  cs->gamutTransformTo(ColorSpace::MakeRGB(namedTransferFn::Linear, namedGamut::Rec2020).get(), &m);
  constexpr float Y_rec2020[3] = {0.262700f, 0.678000f, 0.059300f};
  for (int i = 0; i < 3; ++i) {
    Y[i] = 0.f;
    for (int j = 0; j < 3; ++j) {
      Y[i] += m.vals[j][i] * Y_rec2020[j];
    }
  }
}

ColorSpaceXformSteps::ColorSpaceXformSteps(const ColorSpace* src, AlphaType srcAT,
                                           const ColorSpace* dst, AlphaType dstAT) {
  // Opaque outputs are treated as the same alpha type as the source input.
  if (dstAT == AlphaType::Opaque) {
    dstAT = srcAT;
  }

  // We have some options about what to do with null src or dst here.
  // This pair seems to be the most consistent with legacy expectations.
  if (!src) {
    src = ColorSpace::MakeSRGB().get();
  }
  if (!dst) {
    dst = src;
  }

  if (src->hash() == dst->hash() && srcAT == dstAT) {
    return;
  }

  TransferFunction srcTrfn;
  src->transferFn(&srcTrfn);
  TransferFunction dstTrfn;
  dst->transferFn(&dstTrfn);

  // The scale factor is the amount that values in linear space will be scaled to accommodate
  // peak luminance and HDR reference white luminance.
  float scaleFactor = 1.f;

  switch (gfx::skcms_TransferFunction_getType(
      reinterpret_cast<gfx::skcms_TransferFunction*>(&srcTrfn))) {
    case gfx::skcms_TFType_PQ:
      // PQ is always scaled by a peak luminance of 10,000 nits, then divided by the HDR
      // reference white luminance (a).
      scaleFactor *= 10000.f / srcTrfn.a;
      // Use the default PQish transfer function.
      this->fSrcTF = namedTransferFn::PQ;
      this->fFlags.linearize = true;
      break;
    case gfx::skcms_TFType_HLG:
      // HLG is scaled by the peak luminance (b), then divided by the HDR reference white
      // luminance (a).
      scaleFactor *= srcTrfn.b / srcTrfn.a;
      this->fFlags.linearize = true;
      // Use the HLGish transfer function scaled by 1/12.
      this->fSrcTF = namedTransferFn::HLG;
      this->fSrcTF.f = 1 / 12.f - 1.f;
      // If the system gamma is not 1.0, then compute the parameters for the OOTF.
      if (srcTrfn.c != 1.f) {
        this->fFlags.src_ootf = true;
        this->fSrcOotf[3] = srcTrfn.c - 1.f;
        SetOotfY(src, this->fSrcOotf);
      }
      break;
    default:
      this->fFlags.linearize = memcmp(&srcTrfn, &namedTransferFn::Linear, sizeof(srcTrfn)) != 0;
      if (this->fFlags.linearize) {
        src->transferFn(&this->fSrcTF);
      }
      break;
  }

  switch (gfx::skcms_TransferFunction_getType(
      reinterpret_cast<gfx::skcms_TransferFunction*>(&dstTrfn))) {
    case gfx::skcms_TFType_PQ:
      // This is the inverse of the treatment of source PQ.
      scaleFactor /= 10000.f / dstTrfn.a;
      this->fFlags.encode = true;
      this->fDstTFInv = namedTransferFn::PQ;
      gfx::skcms_TransferFunction_invert(
          reinterpret_cast<gfx::skcms_TransferFunction*>(&this->fDstTFInv),
          reinterpret_cast<gfx::skcms_TransferFunction*>(&this->fDstTFInv));
      break;
    case gfx::skcms_TFType_HLG:
      // This is the inverse of the treatment of source HLG.
      scaleFactor /= dstTrfn.b / dstTrfn.a;
      this->fFlags.encode = true;
      this->fDstTFInv = namedTransferFn::HLG;
      this->fDstTFInv.f = 1 / 12.f - 1.f;
      gfx::skcms_TransferFunction_invert(
          reinterpret_cast<gfx::skcms_TransferFunction*>(&this->fDstTFInv),
          reinterpret_cast<gfx::skcms_TransferFunction*>(&this->fDstTFInv));
      if (dstTrfn.c != 1.f) {
        this->fFlags.dst_ootf = true;
        this->fDstOotf[3] = 1 / dstTrfn.c - 1.f;
        SetOotfY(dst, this->fDstOotf);
      }
      break;
    default:
      this->fFlags.encode = memcmp(&dstTrfn, &namedTransferFn::Linear, sizeof(dstTrfn)) != 0;
      if (this->fFlags.encode) {
        dst->invTransferFn(&this->fDstTFInv);
      }
      break;
  }

  this->fFlags.unpremul = srcAT == AlphaType::Premultiplied;
  this->fFlags.gamut_transform = src->toXYZD50Hash() != dst->toXYZD50Hash() || scaleFactor != 1.f;
  this->fFlags.premul = srcAT != AlphaType::Opaque && dstAT == AlphaType::Premultiplied;

  if (this->fFlags.gamut_transform) {
    Matrix3x3 src_to_dst;
    src->gamutTransformTo(dst, &src_to_dst);

    this->fSrcToDstMatrix[0] = src_to_dst.vals[0][0] * scaleFactor;
    this->fSrcToDstMatrix[1] = src_to_dst.vals[1][0] * scaleFactor;
    this->fSrcToDstMatrix[2] = src_to_dst.vals[2][0] * scaleFactor;

    this->fSrcToDstMatrix[3] = src_to_dst.vals[0][1] * scaleFactor;
    this->fSrcToDstMatrix[4] = src_to_dst.vals[1][1] * scaleFactor;
    this->fSrcToDstMatrix[5] = src_to_dst.vals[2][1] * scaleFactor;

    this->fSrcToDstMatrix[6] = src_to_dst.vals[0][2] * scaleFactor;
    this->fSrcToDstMatrix[7] = src_to_dst.vals[1][2] * scaleFactor;
    this->fSrcToDstMatrix[8] = src_to_dst.vals[2][2] * scaleFactor;
  } else {
#ifdef DEBUG
    Matrix3x3 srcM, dstM;
    src->toXYZD50(&srcM);
    dst->toXYZD50(&dstM);
    DEBUG_ASSERT(0 == memcmp(&srcM, &dstM, 9 * sizeof(float)) && "Hash collision");
#endif
  }

  // If the source and destination OOTFs cancel each other out, skip both.
  if (this->fFlags.src_ootf && !this->fFlags.gamut_transform && this->fFlags.dst_ootf) {
    // If there is no gamut transform, then the r,g,b coefficients for the
    // OOTFs must be the same.
    DEBUG_ASSERT(0 == memcmp(&this->fSrcOotf, &this->fDstOotf, 3 * sizeof(float)));
    // If the gammas cancel out, then remove the steps.
    if ((this->fSrcOotf[3] + 1.f) * (this->fDstOotf[3] + 1.f) == 1.f) {
      this->fFlags.src_ootf = false;
      this->fFlags.dst_ootf = false;
    }
  }

  // If we linearize then immediately reencode with the same transfer function, skip both.
  if (this->fFlags.linearize && !this->fFlags.src_ootf && !this->fFlags.gamut_transform &&
      !this->fFlags.dst_ootf && this->fFlags.encode &&
      src->transferFnHash() == dst->transferFnHash()) {
#ifdef DEBUG
    TransferFunction dstTF;
    dst->transferFn(&dstTF);
    for (int i = 0; i < 7; i++) {
      DEBUG_ASSERT((&fSrcTF.g)[i] == (&dstTF.g)[i] && "Hash collision");
    }
#endif
    this->fFlags.linearize = false;
    this->fFlags.encode = false;
  }

  // Skip unpremul...premul if there are no non-linear operations between.
  if (this->fFlags.unpremul && !this->fFlags.linearize && !this->fFlags.encode &&
      this->fFlags.premul) {
    this->fFlags.unpremul = false;
    this->fFlags.premul = false;
  }
}

void ColorSpaceXformSteps::apply(float rgba[4]) const {
  if (this->fFlags.unpremul) {
    auto is_finite = [](float x) { return x * 0 == 0; };
    float invA = 1.0f / rgba[3];
    invA = is_finite(invA) ? invA : 0;
    rgba[0] *= invA;
    rgba[1] *= invA;
    rgba[2] *= invA;
  }
  if (this->fFlags.linearize) {
    rgba[0] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&fSrcTF), rgba[0]);
    rgba[1] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&fSrcTF), rgba[1]);
    rgba[2] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&fSrcTF), rgba[2]);
  }
  if (this->fFlags.src_ootf) {
    const float Y = fSrcOotf[0] * rgba[0] + fSrcOotf[1] * rgba[1] + fSrcOotf[2] * rgba[2];
    const float Y_to_gamma_minus_1 = std::pow(Y, fSrcOotf[3]);
    rgba[0] *= Y_to_gamma_minus_1;
    rgba[1] *= Y_to_gamma_minus_1;
    rgba[2] *= Y_to_gamma_minus_1;
  }
  if (this->fFlags.gamut_transform) {
    float temp[3] = {rgba[0], rgba[1], rgba[2]};
    for (int i = 0; i < 3; ++i) {
      rgba[i] = fSrcToDstMatrix[i] * temp[0] + fSrcToDstMatrix[3 + i] * temp[1] +
                fSrcToDstMatrix[6 + i] * temp[2];
    }
  }
  if (this->fFlags.dst_ootf) {
    const float Y = fDstOotf[0] * rgba[0] + fDstOotf[1] * rgba[1] + fDstOotf[2] * rgba[2];
    const float Y_to_gamma_minus_1 = std::pow(Y, fDstOotf[3]);
    rgba[0] *= Y_to_gamma_minus_1;
    rgba[1] *= Y_to_gamma_minus_1;
    rgba[2] *= Y_to_gamma_minus_1;
  }
  if (this->fFlags.encode) {
    rgba[0] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&fDstTFInv), rgba[0]);
    rgba[1] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&fDstTFInv), rgba[1]);
    rgba[2] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&fDstTFInv), rgba[2]);
  }
  if (this->fFlags.premul) {
    rgba[0] *= rgba[3];
    rgba[1] *= rgba[3];
    rgba[2] *= rgba[3];
  }
}

uint32_t ColorSpaceXformSteps::xformKey(const ColorSpaceXformSteps* xform) {
  // Code generation depends on which steps we apply,
  // and the kinds of transfer functions (if we're applying those).
  if (!xform) {
    return 0;
  }
  uint32_t key = xform->fFlags.mask();
  if (xform->fFlags.linearize) {
    key |= static_cast<uint32_t>(
        gfx::skcms_TransferFunction_getType(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&xform->fSrcTF))
        << 8);
  }
  if (xform->fFlags.encode) {
    key |= static_cast<uint32_t>(
        gfx::skcms_TransferFunction_getType(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&xform->fDstTFInv))
        << 16);
  }
  return key;
}
}  // namespace tgfx