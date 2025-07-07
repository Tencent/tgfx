/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/processors/TextureEffect.h"

namespace tgfx {
class GLTextureEffect : public TextureEffect {
 public:
  GLTextureEffect(std::shared_ptr<TextureProxy> proxy, const Point& alphaStart,
                  const SamplingOptions& sampling, SrcRectConstraint constraint,
                  const Matrix& uvMatrix, const std::optional<Rect>& subset);

  void emitCode(EmitArgs& args) const override;

 private:
  void emitPlainTextureCode(EmitArgs& args) const;
  void emitYUVTextureCode(EmitArgs& args) const;
  void onSetData(UniformBuffer* uniformBuffer) const override;
  void appendClamp(FragmentShaderBuilder* fragBuilder, const std::string& vertexColor,
                   const std::string& finalCoordName, const std::string& subsetName,
                   const std::string& extraSubsetName) const;
};
}  // namespace tgfx
