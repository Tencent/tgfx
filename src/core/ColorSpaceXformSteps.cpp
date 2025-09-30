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
#include <cmath>
#include <cstring>
#include "tgfx/core/AlphaType.h"
#include "tgfx/core/ColorSpace.h"
#include "utils/Log.h"

namespace tgfx {

// Rec. ITU-R BT.2100-2 perceptual quantization (PQ) system, value 16.
static constexpr TransferFunction PQTF = {
    -2.0f, -107 / 128.0f, 1.0f, 32 / 2523.0f, 2413 / 128.0f, -2392 / 128.0f, 8192 / 1305.0f};

// Rec. ITU-R BT.2100-2 hybrid log-gamma (HLG) system, value 18.
static constexpr TransferFunction HLGTF = {-3.0f,       2.0f,        2.0f, 1 / 0.17883277f,
                                           0.28466892f, 0.55991073f, 0.0f};

// Compute the Y vector for the HLG OOTF in the primaries of a specified color space. The value
// is specified in Rec2020 primaries in ITU-R BT.2100.
static void SetOOTFY(const ColorSpace* cs, float* Y) {
  ColorMatrix33 m;
  cs->gamutTransformTo(ColorSpace::MakeRGB(NamedTransferFn::Linear, NamedGamut::Rec2020).get(), &m);
  constexpr float yRec2020[3] = {0.262700f, 0.678000f, 0.059300f};
  for (int i = 0; i < 3; ++i) {
    Y[i] = 0.f;
    for (int j = 0; j < 3; ++j) {
      Y[i] += m.values[j][i] * yRec2020[j];
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
      this->srcTF = PQTF;
      this->flags.linearize = true;
      break;
    case gfx::skcms_TFType_HLG:
      // HLG is scaled by the peak luminance (b), then divided by the HDR reference white
      // luminance (a).
      scaleFactor *= srcTrfn.b / srcTrfn.a;
      this->flags.linearize = true;
      // Use the HLGish transfer function scaled by 1/12.
      this->srcTF = HLGTF;
      this->srcTF.f = 1 / 12.f - 1.f;
      // If the system gamma is not 1.0, then compute the parameters for the OOTF.
      if (srcTrfn.c != 1.f) {
        this->flags.srcOOTF = true;
        this->srcOotf[3] = srcTrfn.c - 1.f;
        SetOOTFY(src, this->srcOotf);
      }
      break;
    default:
      this->flags.linearize = memcmp(&srcTrfn, &NamedTransferFn::Linear, sizeof(srcTrfn)) != 0;
      if (this->flags.linearize) {
        src->transferFn(&this->srcTF);
      }
      break;
  }

  switch (gfx::skcms_TransferFunction_getType(
      reinterpret_cast<gfx::skcms_TransferFunction*>(&dstTrfn))) {
    case gfx::skcms_TFType_PQ:
      // This is the inverse of the treatment of source PQ.
      scaleFactor /= 10000.f / dstTrfn.a;
      this->flags.encode = true;
      this->dstTFInv = PQTF;
      gfx::skcms_TransferFunction_invert(
          reinterpret_cast<gfx::skcms_TransferFunction*>(&this->dstTFInv),
          reinterpret_cast<gfx::skcms_TransferFunction*>(&this->dstTFInv));
      break;
    case gfx::skcms_TFType_HLG:
      // This is the inverse of the treatment of source HLG.
      scaleFactor /= dstTrfn.b / dstTrfn.a;
      this->flags.encode = true;
      this->dstTFInv = HLGTF;
      this->dstTFInv.f = 1 / 12.f - 1.f;
      gfx::skcms_TransferFunction_invert(
          reinterpret_cast<gfx::skcms_TransferFunction*>(&this->dstTFInv),
          reinterpret_cast<gfx::skcms_TransferFunction*>(&this->dstTFInv));
      if (dstTrfn.c != 1.f) {
        this->flags.dstOOTF = true;
        this->dstOotf[3] = 1 / dstTrfn.c - 1.f;
        SetOOTFY(dst, this->dstOotf);
      }
      break;
    default:
      this->flags.encode = memcmp(&dstTrfn, &NamedTransferFn::Linear, sizeof(dstTrfn)) != 0;
      if (this->flags.encode) {
        dst->invTransferFn(&this->dstTFInv);
      }
      break;
  }

  this->flags.unPremul = srcAT == AlphaType::Premultiplied;
  this->flags.gamutTransform = src->toXYZD50Hash() != dst->toXYZD50Hash() || scaleFactor != 1.f;
  this->flags.premul = srcAT != AlphaType::Opaque && dstAT == AlphaType::Premultiplied;

  if (this->flags.gamutTransform) {
    ColorMatrix33 srcToDst;
    src->gamutTransformTo(dst, &srcToDst);
    srcToDstMatrix = srcToDst * scaleFactor;
  } else {
#ifdef DEBUG
    ColorMatrix33 srcM, dstM;
    src->toXYZD50(&srcM);
    dst->toXYZD50(&dstM);
    DEBUG_ASSERT(0 == memcmp(&srcM, &dstM, 9 * sizeof(float)) && "Hash collision");
#endif
  }

  // If the source and destination OOTFs cancel each other out, skip both.
  if (this->flags.srcOOTF && !this->flags.gamutTransform && this->flags.dstOOTF) {
    // If there is no gamut transform, then the r,g,b coefficients for the
    // OOTFs must be the same.
    DEBUG_ASSERT(0 == memcmp(&this->srcOotf, &this->dstOotf, 3 * sizeof(float)));
    // If the gammas cancel out, then remove the steps.
    if ((this->srcOotf[3] + 1.f) * (this->dstOotf[3] + 1.f) == 1.f) {
      this->flags.srcOOTF = false;
      this->flags.dstOOTF = false;
    }
  }

  // If we linearize then immediately reencode with the same transfer function, skip both.
  if (this->flags.linearize && !this->flags.srcOOTF && !this->flags.gamutTransform &&
      !this->flags.dstOOTF && this->flags.encode &&
      src->transferFnHash() == dst->transferFnHash()) {
#ifdef DEBUG
    TransferFunction dstTF;
    dst->transferFn(&dstTF);
    for (int i = 0; i < 7; i++) {
      DEBUG_ASSERT((&srcTF.g)[i] == (&dstTF.g)[i] && "Hash collision");
    }
#endif
    this->flags.linearize = false;
    this->flags.encode = false;
  }

  // Skip unpremul...premul if there are no non-linear operations between.
  if (this->flags.unPremul && !this->flags.linearize && !this->flags.encode && this->flags.premul) {
    this->flags.unPremul = false;
    this->flags.premul = false;
  }
}

void ColorSpaceXformSteps::apply(float rgba[4]) const {
  if (this->flags.unPremul) {
    auto is_finite = [](float x) { return x * 0 == 0; };
    float invA = 1.0f / rgba[3];
    invA = is_finite(invA) ? invA : 0;
    rgba[0] *= invA;
    rgba[1] *= invA;
    rgba[2] *= invA;
  }
  if (this->flags.linearize) {
    rgba[0] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&srcTF), rgba[0]);
    rgba[1] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&srcTF), rgba[1]);
    rgba[2] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&srcTF), rgba[2]);
  }
  if (this->flags.srcOOTF) {
    const float Y = srcOotf[0] * rgba[0] + srcOotf[1] * rgba[1] + srcOotf[2] * rgba[2];
    const float Y_to_gamma_minus_1 = std::pow(Y, srcOotf[3]);
    rgba[0] *= Y_to_gamma_minus_1;
    rgba[1] *= Y_to_gamma_minus_1;
    rgba[2] *= Y_to_gamma_minus_1;
  }
  if (this->flags.gamutTransform) {
    float temp[3] = {rgba[0], rgba[1], rgba[2]};
    for (int i = 0; i < 3; ++i) {
      rgba[i] = srcToDstMatrix.values[i % 3][i / 3] * temp[0] +
                srcToDstMatrix.values[(3 + i) % 3][(3 + i) / 3] * temp[1] +
                srcToDstMatrix.values[(6 + i) % 3][(6 + i) / 3] * temp[2];
    }
  }
  if (this->flags.dstOOTF) {
    const float Y = dstOotf[0] * rgba[0] + dstOotf[1] * rgba[1] + dstOotf[2] * rgba[2];
    const float Y_to_gamma_minus_1 = std::pow(Y, dstOotf[3]);
    rgba[0] *= Y_to_gamma_minus_1;
    rgba[1] *= Y_to_gamma_minus_1;
    rgba[2] *= Y_to_gamma_minus_1;
  }
  if (this->flags.encode) {
    rgba[0] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&dstTFInv), rgba[0]);
    rgba[1] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&dstTFInv), rgba[1]);
    rgba[2] = gfx::skcms_TransferFunction_eval(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&dstTFInv), rgba[2]);
  }
  if (this->flags.premul) {
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
  uint32_t key = xform->flags.mask();
  if (xform->flags.linearize) {
    key |= static_cast<uint32_t>(
        gfx::skcms_TransferFunction_getType(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&xform->srcTF))
        << 8);
  }
  if (xform->flags.encode) {
    key |= static_cast<uint32_t>(
        gfx::skcms_TransferFunction_getType(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&xform->dstTFInv))
        << 16);
  }
  return key;
}
}  // namespace tgfx