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

#include <optional>
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
class GLSLTiledTextureEffect : public TiledTextureEffect {
 public:
  GLSLTiledTextureEffect(std::shared_ptr<TextureProxy> proxy, const SamplerState& samplerState,
                         SrcRectConstraint constraint, const Matrix& uvMatrix,
                         const std::optional<Rect>& subset,
                         std::shared_ptr<ColorSpace> dstColorSpace);

  void emitCode(EmitArgs& args) const override;

 private:
  struct UniformNames {
    std::string subsetName;
    std::string clampName;
    std::string dimensionsName;
  };

  void onSetData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const override;

  static bool ShaderModeRequiresUnormCoord(TiledTextureEffect::ShaderMode m);

  static bool ShaderModeUsesSubset(TiledTextureEffect::ShaderMode m);

  static bool ShaderModeUsesClamp(TiledTextureEffect::ShaderMode m);

  void readColor(EmitArgs& args, const std::string& dimensionsName, const std::string& coord,
                 const char* out) const;

  void subsetCoord(EmitArgs& args, TiledTextureEffect::ShaderMode mode,
                   const std::string& subsetName, const char* coordSwizzle,
                   const char* subsetStartSwizzle, const char* subsetStopSwizzle,
                   const char* extraCoord, const char* coordWeight) const;

  void clampCoord(EmitArgs& args, bool clamp, const std::string& clampName,
                  const char* coordSwizzle, const char* clampStartSwizzle,
                  const char* clampStopSwizzle) const;

  void clampCoord(EmitArgs& args, const bool useClamp[2], const std::string& clampName) const;

  UniformNames initUniform(EmitArgs& args, const TextureView* textureView, const Sampling& sampling,
                           const bool useClamp[2]) const;
};
}  // namespace tgfx
