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
#include "ColorSpaceXformHelper.h"
#include <algorithm>
#include <array>
#include <iterator>
#include "core/utils/Log.h"
namespace tgfx {

void ColorSpaceXformHelper::emitCode(UniformHandler* uniformHandler,
                                     const ColorSpaceXformSteps* colorSpaceXform,
                                     ShaderStage shaderStage) {
  if (colorSpaceXform) {
    flags = colorSpaceXform->flags;
    if (this->applySrcTF()) {
      srcTFVar0 = uniformHandler->addUniform("SrcTF0", UniformFormat::Float4, shaderStage);
      srcTFVar1 = uniformHandler->addUniform("SrcTF1", UniformFormat::Float4, shaderStage);
      _srcTFType =
          gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
              &colorSpaceXform->srcTransferFunction));
    }
    if (this->applySrcOOTF()) {
      srcOOTFVar = uniformHandler->addUniform("SrcOOTF", UniformFormat::Float4, shaderStage);
    }
    if (this->applyGamutXform()) {
      gamutXformVar =
          uniformHandler->addUniform("ColorXform", UniformFormat::Float3x3, shaderStage);
    }
    if (this->applyDstOOTF()) {
      dstOOTFVar = uniformHandler->addUniform("DstOOTF", UniformFormat::Float4, shaderStage);
    }
    if (this->applyDstTF()) {
      dstTFVar0 = uniformHandler->addUniform("DstTF0", UniformFormat::Float4, shaderStage);
      dstTFVar1 = uniformHandler->addUniform("DstTF1", UniformFormat::Float4, shaderStage);
      _dstTFType =
          gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
              &colorSpaceXform->dstTransferFunctionInverse));
    }
  }
}

void ColorSpaceXformHelper::setData(UniformData* uniformData,
                                    const ColorSpaceXformSteps* colorSpaceXform) {
  if (colorSpaceXform) {
    flags = colorSpaceXform->flags;
    if (this->applySrcTF()) {
      float srcTF0[4] = {
          colorSpaceXform->srcTransferFunction.g, colorSpaceXform->srcTransferFunction.a,
          colorSpaceXform->srcTransferFunction.b, colorSpaceXform->srcTransferFunction.c};
      float srcTF1[4] = {colorSpaceXform->srcTransferFunction.d,
                         colorSpaceXform->srcTransferFunction.e,
                         colorSpaceXform->srcTransferFunction.f, 0.0f};
      uniformData->setData("SrcTF0", srcTF0);
      uniformData->setData("SrcTF1", srcTF1);
    }
    if (this->applySrcOOTF()) {
      uniformData->setData("SrcOOTF", colorSpaceXform->srcOOTF);
    }
    if (this->applyGamutXform()) {
      uniformData->setData("ColorXform", colorSpaceXform->srcToDstMatrix);
    }
    if (this->applyDstOOTF()) {
      uniformData->setData("DstOOTF", colorSpaceXform->dstOOTF);
    }
    if (this->applyDstTF()) {
      float dstTF0[4] = {colorSpaceXform->dstTransferFunctionInverse.g,
                         colorSpaceXform->dstTransferFunctionInverse.a,
                         colorSpaceXform->dstTransferFunctionInverse.b,
                         colorSpaceXform->dstTransferFunctionInverse.c};
      float dstTF1[4] = {colorSpaceXform->dstTransferFunctionInverse.d,
                         colorSpaceXform->dstTransferFunctionInverse.e,
                         colorSpaceXform->dstTransferFunctionInverse.f, 0.0f};
      uniformData->setData("DstTF0", dstTF0);
      uniformData->setData("DstTF1", dstTF1);
    }
  }
}

int TFTypeToIndex(gfx::skcms_TFType type) {
  switch (type) {
    case gfx::skcms_TFType_sRGBish:
      return 0;
    case gfx::skcms_TFType_PQish:
      return 1;
    case gfx::skcms_TFType_HLGish:
      return 2;
    case gfx::skcms_TFType_HLGinvish:
      return 3;
    default:
      return -1;
  }
}

bool ColorSpaceXformStepsSupported(const ColorSpaceXformSteps* steps) {
  if (steps == nullptr) {
    return false;
  }
  if (steps->flags.linearize &&
      TFTypeToIndex(gfx::skcms_TransferFunction_getType(
          reinterpret_cast<const gfx::skcms_TransferFunction*>(&steps->srcTransferFunction))) < 0) {
    return false;
  }
  if (steps->flags.encode && TFTypeToIndex(gfx::skcms_TransferFunction_getType(
                                 reinterpret_cast<const gfx::skcms_TransferFunction*>(
                                     &steps->dstTransferFunctionInverse))) < 0) {
    return false;
  }
  return true;
}

namespace {

// One color-space field: the addressing-neutral key the element writer resolves to a module's
// declaration index.
enum class CSField {
  CSFlags,
  SrcTF0,
  SrcTF1,
  SrcTFType,
  SrcOOTF,
  ColorXform,
  DstOOTF,
  DstTF0,
  DstTF1,
  DstTFType,
};

ColorMatrix33 CSIdentityMatrix() {
  return {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
}

// The std140 packing of a mat3 uniform: three vec4 columns.
std::array<float, 12> PackColorXform(const ColorMatrix33& matrix) {
  return {matrix.values[0][0], matrix.values[1][0], matrix.values[2][0], 0.0f,
          matrix.values[0][1], matrix.values[1][1], matrix.values[2][1], 0.0f,
          matrix.values[0][2], matrix.values[1][2], matrix.values[2][2], 0.0f};
}

}  // namespace

void ResolveColorSpaceXformValues(const ColorSpaceXformSteps* colorSpaceXform,
                                  ColorSpaceXformValues* out) {
  if (colorSpaceXform == nullptr || out == nullptr) {
    return;
  }
  const auto flags = colorSpaceXform->flags;
  out->flags = static_cast<int>(flags.mask());
  if (flags.linearize) {
    const float srcTF0[4] = {
        colorSpaceXform->srcTransferFunction.g, colorSpaceXform->srcTransferFunction.a,
        colorSpaceXform->srcTransferFunction.b, colorSpaceXform->srcTransferFunction.c};
    const float srcTF1[4] = {colorSpaceXform->srcTransferFunction.d,
                             colorSpaceXform->srcTransferFunction.e,
                             colorSpaceXform->srcTransferFunction.f, 0.0f};
    std::copy(srcTF0, srcTF0 + 4, out->srcTF0);
    std::copy(srcTF1, srcTF1 + 4, out->srcTF1);
    const int srcType = TFTypeToIndex(
        gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
            &colorSpaceXform->srcTransferFunction)));
    if (srcType < 0) {
      // The matcher rejects unsupported transfer functions before a program is created, so
      // reaching here means an entry point bypassed that check. Report loudly instead of
      // silently rendering with the sRGB branch.
      LOGE(
          "ColorSpaceXformHelper: unsupported source transfer function; the draw reached the "
          "upload path without the matcher rejecting it");
    }
    out->srcTFType = srcType < 0 ? 0 : srcType;
  }
  if (flags.srcOOTF) {
    std::copy(std::begin(colorSpaceXform->srcOOTF), std::end(colorSpaceXform->srcOOTF),
              out->srcOOTF);
  }
  const auto& matrix = flags.gamutTransform ? colorSpaceXform->srcToDstMatrix : CSIdentityMatrix();
  const auto packed = PackColorXform(matrix);
  std::copy(packed.begin(), packed.end(), out->colorXform);
  if (flags.dstOOTF) {
    std::copy(std::begin(colorSpaceXform->dstOOTF), std::end(colorSpaceXform->dstOOTF),
              out->dstOOTF);
  }
  if (flags.encode) {
    const float dstTF0[4] = {colorSpaceXform->dstTransferFunctionInverse.g,
                             colorSpaceXform->dstTransferFunctionInverse.a,
                             colorSpaceXform->dstTransferFunctionInverse.b,
                             colorSpaceXform->dstTransferFunctionInverse.c};
    const float dstTF1[4] = {colorSpaceXform->dstTransferFunctionInverse.d,
                             colorSpaceXform->dstTransferFunctionInverse.e,
                             colorSpaceXform->dstTransferFunctionInverse.f, 0.0f};
    std::copy(dstTF0, dstTF0 + 4, out->dstTF0);
    std::copy(dstTF1, dstTF1 + 4, out->dstTF1);
    const int dstType = TFTypeToIndex(
        gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
            &colorSpaceXform->dstTransferFunctionInverse)));
    if (dstType < 0) {
      LOGE(
          "ColorSpaceXformHelper: unsupported destination transfer function; the draw reached "
          "the upload path without the matcher rejecting it");
    }
    out->dstTFType = dstType < 0 ? 0 : dstType;
  }
}

}  // namespace tgfx
