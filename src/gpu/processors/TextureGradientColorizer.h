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

#include <utility>
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/variants/ShaderVariant.h"

namespace tgfx {
class TextureGradientColorizer : public FragmentProcessor {
 public:
  static PlacementPtr<TextureGradientColorizer> Make(BlockAllocator* allocator,
                                                     std::shared_ptr<TextureProxy> gradient);

  std::string name() const override {
    return "TextureGradientColorizer";
  }

  /** Returns the single trivial shader variant (no compile-time macros). */
  static std::vector<ShaderVariant> EnumerateVariants() {
    return MakeTrivialShaderVariantList("TextureGradientColorizer");
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit TextureGradientColorizer(std::shared_ptr<TextureProxy> gradient)
      : FragmentProcessor(ClassID()), gradient(std::move(gradient)) {
  }

  std::string shaderFunctionFile() const override {
    return "fragment/texture_gradient_colorizer.frag";
  }

  ShaderCallManifest buildCallStatement(const std::string& inputColorVar, int fpIndex,
                                        const MangledUniforms& /*uniforms*/,
                                        const MangledVaryings& /*varyings*/,
                                        const MangledSamplers& samplers) const override {
    ShaderCallManifest manifest;
    manifest.functionName = "TGFX_TextureGradientColorizer";
    manifest.outputVarName = "color_fp" + std::to_string(fpIndex);
    manifest.includeFiles = {shaderFunctionFile()};
    manifest.argExpressions = {inputColorVar.empty() ? std::string("vec4(1.0)") : inputColorVar,
                               samplers.getByIndex(0)};
    return manifest;
  }

  size_t onCountTextureSamplers() const override {
    return 1;
  }

  std::shared_ptr<Texture> onTextureAt(size_t) const override {
    auto textureView = gradient->getTextureView();
    return textureView ? textureView->getTexture() : nullptr;
  }

  std::shared_ptr<TextureProxy> gradient;
};
}  // namespace tgfx
