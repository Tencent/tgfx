/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "gpu/BlendFormula.h"
#include "gpu/processors/XferProcessor.h"
#include "gpu/variants/ShaderVariant.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {
class PorterDuffXferProcessor : public XferProcessor {
 public:
  static PlacementPtr<PorterDuffXferProcessor> Make(BlockAllocator* allocator, BlendMode blend,
                                                    DstTextureInfo dstTextureInfo);

  std::string name() const override {
    return "PorterDuffXferProcessor";
  }

  const TextureView* dstTextureView() const override;

  void computeProcessorKey(Context* context, BytesKey* bytesKey) const override;

  /**
   * Populates the given ShaderMacroSet with the preprocessor defines this processor would emit
   * for the specified (blendMode, hasDstTexture) configuration. This is the single source of
   * truth for both the runtime path (onBuildShaderMacros) and the offline variant enumerator
   * (EnumerateVariants). Extracted as static so variant tooling can call it without constructing
   * a PorterDuffXferProcessor instance.
   */
  static void BuildMacros(BlendMode blendMode, bool hasDstTexture, ShaderMacroSet& macros);

  /**
   * Returns the full set of shader variants this processor can produce. The result is the
   * Cartesian product (30 BlendMode values) x (hasDstTexture in {false, true}) = 60 variants.
   * Illegal combinations (e.g. non-coeff modes without dst texture on backends that lack
   * framebuffer fetch) are still emitted — callers/offline tooling can filter as needed. Each
   * variant carries a preamble string that, prefixed to porter_duff_xfer.frag.glsl, yields the
   * exact shader source the runtime would compile for that configuration.
   */
  static std::vector<ShaderVariant> EnumerateVariants();

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  PorterDuffXferProcessor(BlendMode blend, DstTextureInfo dstTextureInfo)
      : XferProcessor(ClassID()), blendMode(blend), dstTextureInfo(std::move(dstTextureInfo)) {
  }

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    BuildMacros(blendMode, dstTextureInfo.textureProxy != nullptr, macros);
  }

  std::string shaderFunctionFile() const override {
    return "xfer/porter_duff_xfer.frag";
  }

  ShaderCallManifest buildXferCallStatement(const std::string& colorInVar,
                                            const std::string& coverageInVar,
                                            const std::string& outputVar,
                                            const std::string& dstColorExpr,
                                            const MangledUniforms& uniforms,
                                            const MangledSamplers& samplers) const override;

  BlendMode blendMode = BlendMode::SrcOver;
  DstTextureInfo dstTextureInfo = {};
};
}  // namespace tgfx
