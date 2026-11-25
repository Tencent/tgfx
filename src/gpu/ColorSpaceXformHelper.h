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
#include <cstdint>
#include <string>
#include "UniformData.h"
#include "UniformHandler.h"
#include "core/ColorSpaceXformSteps.h"
#include "gpu/ShaderVar.h"

namespace tgfx {

/**
 * The JIT route's emission-side view of one color-space transform: the uniforms the emitted GLSL
 * reads and the flags that gate them. The precompiled route's resolvers below share the same
 * steps but spell the values through the bundle's std140 contract instead.
 */
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
  /// The active steps, in the shader's flag encoding (see ColorSpaceXformSteps::Flags::mask).
  uint32_t flagsMask() const {
    return flags.mask();
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

/**
 * Maps an skcms transfer-function kind to the TFType uniform index the shaders switch on.
 * Returns -1 for kinds the precompiled math does not implement: the matcher treats that as
 * "this color space cannot ride the AOT path" (the draw falls back to runtime), and the upload
 * path treats it as a contract violation to report loudly — never as a silent downgrade to the
 * sRGB branch. This is the single authority for the mapping; do not re-derive it locally.
 */
int TFTypeToIndex(gfx::skcms_TFType type);

/**
 * Single admission gate for a color-space step set riding any precompiled path. Returns false
 * when an enabled linearize/encode step carries a transfer function whose kind the shared math
 * does not implement: the matcher already rejects such steps for the standalone effect, and this
 * gate extends the same rule to the owned-slot paths (tail / YUV / Perlin / chain) and the graph
 * builder, so an unsupported transfer function fails before a program is created instead of the
 * upload path logging and silently rendering with the sRGB branch.
 */
bool ColorSpaceXformStepsSupported(const ColorSpaceXformSteps* steps);

/**
 * One color-space transform's parameter values, in the std140 packing every module's block spells:
 * the flags bitmask, the transfer-function pairs (zero when the step is disabled, matching the
 * flags bit that keeps them unread), the OOTF vectors, and the gamut matrix as three vec4 columns
 * or its identity stand-in. One resolver feeds both the element-indexed module writers and the
 * assembled chain's named members, so the two cannot pack a step differently.
 */
struct ColorSpaceXformValues {
  int flags = 0;
  float srcTF0[4] = {};
  float srcTF1[4] = {};
  int srcTFType = 0;
  float srcOOTF[4] = {};
  float colorXform[12] = {};
  float dstOOTF[4] = {};
  float dstTF0[4] = {};
  float dstTF1[4] = {};
  int dstTFType = 0;
};

/// Resolves one step set into the values above. A null step set resolves to nothing.
void ResolveColorSpaceXformValues(const ColorSpaceXformSteps* colorSpaceXform,
                                  ColorSpaceXformValues* out);

}  // namespace tgfx
