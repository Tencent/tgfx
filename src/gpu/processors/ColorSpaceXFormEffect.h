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
#include <vector>
#include "FragmentProcessor.h"
#include "core/ColorSpaceXformSteps.h"
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/variants/ShaderVariant.h"

namespace tgfx {

class ColorSpaceXformEffect : public FragmentProcessor {
 public:
  /**
   * Bit layout for the canonical ColorSpaceXformEffect variant key. Offline tooling should use
   * these masks to pack/unpack the `variant.index` returned by EnumerateVariants().
   *
   * Layout (from LSB to MSB):
   *   bit 0:      unPremul       (TGFX_CSX_UNPREMUL)
   *   bit 1:      srcOOTF        (TGFX_CSX_SRC_OOTF)
   *   bit 2:      gamutXform     (TGFX_CSX_GAMUT_XFORM)
   *   bit 3:      dstOOTF        (TGFX_CSX_DST_OOTF)
   *   bit 4:      premul         (TGFX_CSX_PREMUL)
   *   bits 5..7:  srcTF: 0 = disabled, 1..7 = skcms_TFType value + 1
   *               (TGFX_CSX_SRC_TF / TGFX_CSX_SRC_TF_TYPE — 8 values, 3 bits)
   *   bits 8..10: dstTF: same encoding as srcTF (TGFX_CSX_DST_TF / TGFX_CSX_DST_TF_TYPE)
   *
   * skcms_TFType has 7 values (Invalid..HLG) — when srcTF/dstTF bits equal 0 the respective TF
   * macro pair is omitted from the preamble; otherwise srcTFBits - 1 equals the
   * skcms_TFType value passed as the TGFX_CSX_*_TF_TYPE macro value.
   *
   * Total variant space: 2 * 2 * 2 * 2 * 2 * 8 * 8 = 2048.
   */
  static constexpr int kSrcTFBits = 3;
  static constexpr int kDstTFBits = 3;
  static constexpr int kTFValueCount = 8;  // 0 = disabled, 1..7 = TFType values
  static constexpr int kVariantCount = 2 * 2 * 2 * 2 * 2 * kTFValueCount * kTFValueCount;

  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator, ColorSpace* src,
                                              AlphaType srcAT, ColorSpace* dst, AlphaType dstAT);

  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              std::shared_ptr<ColorSpaceXformSteps> colorXform);

  ColorSpaceXformEffect(std::shared_ptr<ColorSpaceXformSteps> colorXform);

  std::string name() const override {
    return "ColorSpaceXformEffect";
  }

  const ColorSpaceXformSteps* colorXform() const {
    return colorSpaceXformSteps.get();
  }

  /**
   * Populates the given ShaderMacroSet with the preprocessor defines this FP emits for the
   * specified configuration. srcTFType / dstTFType should be 0 when the corresponding TF step is
   * disabled; otherwise they should equal the skcms_TFType value (cast to int).
   */
  static void BuildMacros(bool unPremul, bool srcTF, int srcTFType, bool srcOOTF, bool gamutXform,
                          bool dstOOTF, bool dstTF, int dstTFType, bool premul,
                          ShaderMacroSet& macros);

  /**
   * Returns all kVariantCount shader variants. Index encoding follows the bit layout documented
   * at the top of this class.
   */
  static std::vector<ShaderVariant> EnumerateVariants();

 protected:
  void onBuildShaderMacros(ShaderMacroSet& macros) const override;

  std::string shaderFunctionFile() const override {
    return "fragment/color_space_xform.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& samplers) const override;

  ShaderCallManifest buildCallStatement(const std::string& inputColorVar, int fpIndex,
                                        const MangledUniforms& uniforms,
                                        const MangledVaryings& varyings,
                                        const MangledSamplers& samplers) const override;

 private:
  DEFINE_PROCESSOR_CLASS_ID
  void onSetData(UniformData*, UniformData*) const override;
  void onComputeProcessorKey(BytesKey* bytesKey) const override;
  std::shared_ptr<ColorSpaceXformSteps> colorSpaceXformSteps;
};
}  // namespace tgfx
