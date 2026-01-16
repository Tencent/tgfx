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
#include "UniformData.h"
#include "UniformHandler.h"
#include "core/ColorSpaceXformSteps.h"
#include "gpu/ShaderVar.h"

namespace tgfx {
class ColorSpaceXformHelper {
 public:
  ColorSpaceXformHelper() = default;

  void emitCode(UniformHandler* uniformHandler, const ColorSpaceXformSteps* colorSpaceXform,
                ShaderStage shaderStage = ShaderStage::Fragment);

  void setData(UniformData* uniformData, const ColorSpaceXformSteps* colorSpaceXform);

  bool isNoop() const {
    return (0 == flags.mask());
  }

  bool applyUnpremul() const {
    return flags.unPremul;
  }
  bool applySrcTF() const {
    return flags.linearize;
  }
  bool applySrcOOTF() const {
    return flags.srcOOTF;
  }
  bool applyGamutXform() const {
    return flags.gamutTransform;
  }
  bool applyDstOOTF() const {
    return flags.dstOOTF;
  }
  bool applyDstTF() const {
    return flags.encode;
  }
  bool applyPremul() const {
    return flags.premul;
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
  ColorSpaceXformSteps::Flags flags;
  gfx::skcms_TFType _srcTFType;
  gfx::skcms_TFType _dstTFType;
};
}  // namespace tgfx
