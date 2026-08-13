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
      srcTFVar0 = declare(uniformHandler, "SrcTF0", UniformFormat::Float4, shaderStage);
      srcTFVar1 = declare(uniformHandler, "SrcTF1", UniformFormat::Float4, shaderStage);
      _srcTFType =
          gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
              &colorSpaceXform->srcTransferFunction));
    }
    if (this->applySrcOOTF()) {
      srcOOTFVar = declare(uniformHandler, "SrcOOTF", UniformFormat::Float4, shaderStage);
    }
    if (this->applyGamutXform()) {
      gamutXformVar = declare(uniformHandler, "ColorXform", UniformFormat::Float3x3, shaderStage);
    }
    if (this->applyDstOOTF()) {
      dstOOTFVar = declare(uniformHandler, "DstOOTF", UniformFormat::Float4, shaderStage);
    }
    if (this->applyDstTF()) {
      dstTFVar0 = declare(uniformHandler, "DstTF0", UniformFormat::Float4, shaderStage);
      dstTFVar1 = declare(uniformHandler, "DstTF1", UniformFormat::Float4, shaderStage);
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
  writeOptional(uniformData, "CSFlags", static_cast<int>(flags.mask()));
  // Select the ColorSpaceXform operator in the shared pointwise operator table (absent elsewhere).
  writeOptional(uniformData, "OpType", 3);

  if (this->applySrcTF()) {
    float srcTF0[4] = {
        colorSpaceXform->srcTransferFunction.g, colorSpaceXform->srcTransferFunction.a,
        colorSpaceXform->srcTransferFunction.b, colorSpaceXform->srcTransferFunction.c};
    float srcTF1[4] = {colorSpaceXform->srcTransferFunction.d,
                       colorSpaceXform->srcTransferFunction.e,
                       colorSpaceXform->srcTransferFunction.f, 0.0f};
    writeRequired(uniformData, "SrcTF0", srcTF0);
    writeRequired(uniformData, "SrcTF1", srcTF1);
    int srcType = TFTypeToIndex(
        gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
            &colorSpaceXform->srcTransferFunction)));
    writeOptional(uniformData, "SrcTFType", srcType);
  } else {
    writeOptional(uniformData, "SrcTF0", kZero4);
    writeOptional(uniformData, "SrcTF1", kZero4);
    writeOptional(uniformData, "SrcTFType", 0);
  }

  if (this->applySrcOOTF()) {
    writeRequired(uniformData, "SrcOOTF", colorSpaceXform->srcOOTF);
  } else {
    writeOptional(uniformData, "SrcOOTF", kZero4);
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
    uniformData->setArrayElementOptional("ColorXform", static_cast<size_t>(slot), packed);
  } else if (this->applyGamutXform()) {
    uniformData->setData(prefix + "ColorXform", colorSpaceXform->srcToDstMatrix);
  } else if (uniformData->hasField(prefix + "ColorXform")) {
    ColorMatrix33 identity = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    uniformData->setData(prefix + "ColorXform", identity);
  }

  if (this->applyDstOOTF()) {
    writeRequired(uniformData, "DstOOTF", colorSpaceXform->dstOOTF);
  } else {
    writeOptional(uniformData, "DstOOTF", kZero4);
  }

  if (this->applyDstTF()) {
    float dstTF0[4] = {colorSpaceXform->dstTransferFunctionInverse.g,
                       colorSpaceXform->dstTransferFunctionInverse.a,
                       colorSpaceXform->dstTransferFunctionInverse.b,
                       colorSpaceXform->dstTransferFunctionInverse.c};
    float dstTF1[4] = {colorSpaceXform->dstTransferFunctionInverse.d,
                       colorSpaceXform->dstTransferFunctionInverse.e,
                       colorSpaceXform->dstTransferFunctionInverse.f, 0.0f};
    writeRequired(uniformData, "DstTF0", dstTF0);
    writeRequired(uniformData, "DstTF1", dstTF1);
    int dstType = TFTypeToIndex(
        gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
            &colorSpaceXform->dstTransferFunctionInverse)));
    writeOptional(uniformData, "DstTFType", dstType);
  } else {
    writeOptional(uniformData, "DstTF0", kZero4);
    writeOptional(uniformData, "DstTF1", kZero4);
    writeOptional(uniformData, "DstTFType", 0);
  }
}
}  // namespace tgfx
