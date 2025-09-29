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
#include <skcms.h>
#include "UniformBuffer.h"
#include "UniformHandler.h"
#include "gpu/ShaderVar.h"
#include "tgfx/core/ColorSpaceXformSteps.h"

namespace tgfx {
class ColorSpaceXformHelper {
 public:
  ColorSpaceXformHelper() = default;

  void emitCode(UniformHandler* uniformHandler, const ColorSpaceXformSteps* colorSpaceXform,
                ShaderStage shaderStage = ShaderStage::Fragment) {
    if (colorSpaceXform) {
      fFlags = colorSpaceXform->fFlags;
      if (this->applySrcTF()) {
        srcTFVar0 = uniformHandler->addUniform("SrcTF0", UniformFormat::Float4, shaderStage);
        srcTFVar1 = uniformHandler->addUniform("SrcTF1", UniformFormat::Float4, shaderStage);
        _srcTFType = gfx::skcms_TransferFunction_getType(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&colorSpaceXform->fSrcTF));
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
        _dstTFType = gfx::skcms_TransferFunction_getType(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&colorSpaceXform->fDstTFInv));
      }
    }
  }

  void setData(UniformBuffer* uniformBuffer, const ColorSpaceXformSteps* colorSpaceXform) {
    if (colorSpaceXform) {
      fFlags = colorSpaceXform->fFlags;
    }
    if (this->applySrcTF()) {
      float srcTF0[4] = {colorSpaceXform->fSrcTF.g, colorSpaceXform->fSrcTF.a,
                         colorSpaceXform->fSrcTF.b, colorSpaceXform->fSrcTF.c};
      float srcTF1[4] = {colorSpaceXform->fSrcTF.d, colorSpaceXform->fSrcTF.e,
                         colorSpaceXform->fSrcTF.f, 0.0f};
      uniformBuffer->setData("SrcTF0", srcTF0);
      uniformBuffer->setData("SrcTF1", srcTF1);
    }
    if (this->applySrcOOTF()) {
      uniformBuffer->setData("SrcOOTF", colorSpaceXform->fSrcOotf);
    }
    if (this->applyGamutXform()) {
      uniformBuffer->setData("ColorXform", colorSpaceXform->fSrcToDstMatrix);
    }
    if (this->applyDstOOTF()) {
      uniformBuffer->setData("DstOOTF", colorSpaceXform->fDstOotf);
    }
    if (this->applyDstTF()) {
      float dstTF0[4] = {colorSpaceXform->fDstTFInv.g, colorSpaceXform->fDstTFInv.a,
                         colorSpaceXform->fDstTFInv.b, colorSpaceXform->fDstTFInv.c};
      float dstTF1[4] = {colorSpaceXform->fDstTFInv.d, colorSpaceXform->fDstTFInv.e,
                         colorSpaceXform->fDstTFInv.f, 0.0f};
      uniformBuffer->setData("DstTF0", dstTF0);
      uniformBuffer->setData("DstTF1", dstTF1);
    }
  }

  bool isNoop() const {
    return (0 == fFlags.mask());
  }

  bool applyUnpremul() const {
    return fFlags.unPremul;
  }
  bool applySrcTF() const {
    return fFlags.linearize;
  }
  bool applySrcOOTF() const {
    return fFlags.srcOOTF;
  }
  bool applyGamutXform() const {
    return fFlags.gamutTransform;
  }
  bool applyDstOOTF() const {
    return fFlags.dstOOTF;
  }
  bool applyDstTF() const {
    return fFlags.encode;
  }
  bool applyPremul() const {
    return fFlags.premul;
  }

  gfx::skcms_TFType srcTFType() const {
    return _srcTFType;
  }
  gfx::skcms_TFType dstTFType() const {
    return _dstTFType;
  }

  const std::string& srcTFUniform0() const {
    return srcTFVar0;
  }
  const std::string& srcTFUniform1() const {
    return srcTFVar1;
  }
  const std::string& srcOOTFUniform() const {
    return srcOOTFVar;
  }
  const std::string& gamutXformUniform() const {
    return gamutXformVar;
  }
  const std::string& dstOOTFUniform() const {
    return dstOOTFVar;
  }
  const std::string& dstTFUniform0() const {
    return dstTFVar0;
  }
  const std::string& dstTFUniform1() const {
    return dstTFVar1;
  }

 private:
  std::string srcTFVar0;
  std::string srcTFVar1;
  std::string srcOOTFVar;
  std::string gamutXformVar;
  std::string dstOOTFVar;
  std::string dstTFVar0;
  std::string dstTFVar1;
  ColorSpaceXformSteps::Flags fFlags;
  gfx::skcms_TFType _srcTFType;
  gfx::skcms_TFType _dstTFType;
};
}  // namespace tgfx