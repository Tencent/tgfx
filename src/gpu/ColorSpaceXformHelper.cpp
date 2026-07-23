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
namespace tgfx {

static int TFTypeToIndex(gfx::skcms_TFType type) {
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
      return 0;
  }
}

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
  if (!colorSpaceXform) {
    return;
  }
  flags = colorSpaceXform->flags;

  // The precompiled shader folds the pipeline steps into the CSFlags bitmask uniform and always
  // declares every color-space uniform. The runtime-generated shader declares neither CSFlags nor
  // the uniforms of disabled steps, so setDataOptional tolerates their absence there. Disabled
  // steps get identity placeholders: their CSFlags bit is 0, so the shader never reads them.
  static const float kZero4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  uniformData->setDataOptional("CSFlags", static_cast<int>(flags.mask()));

  if (this->applySrcTF()) {
    float srcTF0[4] = {
        colorSpaceXform->srcTransferFunction.g, colorSpaceXform->srcTransferFunction.a,
        colorSpaceXform->srcTransferFunction.b, colorSpaceXform->srcTransferFunction.c};
    float srcTF1[4] = {colorSpaceXform->srcTransferFunction.d,
                       colorSpaceXform->srcTransferFunction.e,
                       colorSpaceXform->srcTransferFunction.f, 0.0f};
    uniformData->setData("SrcTF0", srcTF0);
    uniformData->setData("SrcTF1", srcTF1);
    int srcType = TFTypeToIndex(
        gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
            &colorSpaceXform->srcTransferFunction)));
    uniformData->setDataOptional("SrcTFType", srcType);
  } else {
    uniformData->setDataOptional("SrcTF0", kZero4);
    uniformData->setDataOptional("SrcTF1", kZero4);
    uniformData->setDataOptional("SrcTFType", 0);
  }

  if (this->applySrcOOTF()) {
    uniformData->setData("SrcOOTF", colorSpaceXform->srcOOTF);
  } else {
    uniformData->setDataOptional("SrcOOTF", kZero4);
  }

  if (this->applyGamutXform()) {
    uniformData->setData("ColorXform", colorSpaceXform->srcToDstMatrix);
  } else if (uniformData->hasField("ColorXform")) {
    ColorMatrix33 identity = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    uniformData->setData("ColorXform", identity);
  }

  if (this->applyDstOOTF()) {
    uniformData->setData("DstOOTF", colorSpaceXform->dstOOTF);
  } else {
    uniformData->setDataOptional("DstOOTF", kZero4);
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
    int dstType = TFTypeToIndex(
        gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
            &colorSpaceXform->dstTransferFunctionInverse)));
    uniformData->setDataOptional("DstTFType", dstType);
  } else {
    uniformData->setDataOptional("DstTF0", kZero4);
    uniformData->setDataOptional("DstTF1", kZero4);
    uniformData->setDataOptional("DstTFType", 0);
  }
}
}  // namespace tgfx
