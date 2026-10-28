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
#include "core/utils/Log.h"
#include "gpu/shaders/modules/ColorSpaceParams.h"
#include "gpu/shaders/modules/PointwiseOpModule.h"
namespace tgfx {

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

std::string ColorSpaceXformHelper::declare(UniformHandler* uniformHandler, const std::string& field,
                                           UniformFormat format, ShaderStage stage) {
  if (slot >= 0) {
    auto mangled = uniformHandler->addUniform(field, format, stage, capacity);
    return mangled + "[" + std::to_string(slot) + "]";
  }
  return uniformHandler->addUniform(prefix + field, format, stage);
}

void ColorSpaceXformHelper::emitCode(UniformHandler* uniformHandler,
                                     const ColorSpaceXformSteps* colorSpaceXform,
                                     ShaderStage shaderStage) {
  if (colorSpaceXform) {
    flags = colorSpaceXform->flags;
    if (this->applySrcTF()) {
      srcTFVar0 = declare(uniformHandler, ColorSpaceParams::SrcTF0.name, UniformFormat::Float4,
                          shaderStage);
      srcTFVar1 = declare(uniformHandler, ColorSpaceParams::SrcTF1.name, UniformFormat::Float4,
                          shaderStage);
      _srcTFType =
          gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
              &colorSpaceXform->srcTransferFunction));
    }
    if (this->applySrcOOTF()) {
      srcOOTFVar = declare(uniformHandler, ColorSpaceParams::SrcOOTF.name, UniformFormat::Float4,
                           shaderStage);
    }
    if (this->applyGamutXform()) {
      gamutXformVar = declare(uniformHandler, ColorSpaceParams::ColorXform.name,
                              UniformFormat::Float3x3, shaderStage);
    }
    if (this->applyDstOOTF()) {
      dstOOTFVar = declare(uniformHandler, ColorSpaceParams::DstOOTF.name, UniformFormat::Float4,
                           shaderStage);
    }
    if (this->applyDstTF()) {
      dstTFVar0 = declare(uniformHandler, ColorSpaceParams::DstTF0.name, UniformFormat::Float4,
                          shaderStage);
      dstTFVar1 = declare(uniformHandler, ColorSpaceParams::DstTF1.name, UniformFormat::Float4,
                          shaderStage);
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
  static const float Zero4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  writeOptional(uniformData, ColorSpaceParams::CSFlags.name, static_cast<int>(flags.mask()));
  // Select the ColorSpaceXform operator in the shared pointwise operator table (absent elsewhere).
  writeOptional(uniformData, PointwiseOpModule::OpType.name, 3);

  if (this->applySrcTF()) {
    float srcTF0[4] = {
        colorSpaceXform->srcTransferFunction.g, colorSpaceXform->srcTransferFunction.a,
        colorSpaceXform->srcTransferFunction.b, colorSpaceXform->srcTransferFunction.c};
    float srcTF1[4] = {colorSpaceXform->srcTransferFunction.d,
                       colorSpaceXform->srcTransferFunction.e,
                       colorSpaceXform->srcTransferFunction.f, 0.0f};
    writeRequired(uniformData, ColorSpaceParams::SrcTF0.name, srcTF0);
    writeRequired(uniformData, ColorSpaceParams::SrcTF1.name, srcTF1);
    int srcType = TFTypeToIndex(
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
    writeOptional(uniformData, ColorSpaceParams::SrcTFType.name, srcType < 0 ? 0 : srcType);
  } else {
    writeOptional(uniformData, ColorSpaceParams::SrcTF0.name, Zero4);
    writeOptional(uniformData, ColorSpaceParams::SrcTF1.name, Zero4);
    writeOptional(uniformData, ColorSpaceParams::SrcTFType.name, 0);
  }

  if (this->applySrcOOTF()) {
    writeRequired(uniformData, ColorSpaceParams::SrcOOTF.name, colorSpaceXform->srcOOTF);
  } else {
    writeOptional(uniformData, ColorSpaceParams::SrcOOTF.name, Zero4);
  }

  if (slot >= 0) {
    const ColorMatrix33& m =
        this->applyGamutXform()
            ? colorSpaceXform->srcToDstMatrix
            : ColorMatrix33{{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    // std140 mat3 array element: three vec4 columns.
    float packed[12] = {m.values[0][0], m.values[1][0], m.values[2][0], 0.0f,
                        m.values[0][1], m.values[1][1], m.values[2][1], 0.0f,
                        m.values[0][2], m.values[1][2], m.values[2][2], 0.0f};
    uniformData->setArrayElementOptional(ColorSpaceParams::ColorXform.name,
                                         static_cast<size_t>(slot), packed);
  } else if (this->applyGamutXform()) {
    uniformData->setData(prefix + ColorSpaceParams::ColorXform.name,
                         colorSpaceXform->srcToDstMatrix);
  } else if (uniformData->hasField(prefix + ColorSpaceParams::ColorXform.name)) {
    ColorMatrix33 identity = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    uniformData->setData(prefix + ColorSpaceParams::ColorXform.name, identity);
  }

  if (this->applyDstOOTF()) {
    writeRequired(uniformData, ColorSpaceParams::DstOOTF.name, colorSpaceXform->dstOOTF);
  } else {
    writeOptional(uniformData, ColorSpaceParams::DstOOTF.name, Zero4);
  }

  if (this->applyDstTF()) {
    float dstTF0[4] = {colorSpaceXform->dstTransferFunctionInverse.g,
                       colorSpaceXform->dstTransferFunctionInverse.a,
                       colorSpaceXform->dstTransferFunctionInverse.b,
                       colorSpaceXform->dstTransferFunctionInverse.c};
    float dstTF1[4] = {colorSpaceXform->dstTransferFunctionInverse.d,
                       colorSpaceXform->dstTransferFunctionInverse.e,
                       colorSpaceXform->dstTransferFunctionInverse.f, 0.0f};
    writeRequired(uniformData, ColorSpaceParams::DstTF0.name, dstTF0);
    writeRequired(uniformData, ColorSpaceParams::DstTF1.name, dstTF1);
    int dstType = TFTypeToIndex(
        gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
            &colorSpaceXform->dstTransferFunctionInverse)));
    if (dstType < 0) {
      LOGE(
          "ColorSpaceXformHelper: unsupported destination transfer function; the draw reached "
          "the upload path without the matcher rejecting it");
    }
    writeOptional(uniformData, ColorSpaceParams::DstTFType.name, dstType < 0 ? 0 : dstType);
  } else {
    writeOptional(uniformData, ColorSpaceParams::DstTF0.name, Zero4);
    writeOptional(uniformData, ColorSpaceParams::DstTF1.name, Zero4);
    writeOptional(uniformData, ColorSpaceParams::DstTFType.name, 0);
  }
}
}  // namespace tgfx
