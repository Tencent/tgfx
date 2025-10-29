/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/gpu/RuntimeEffect.h"
#include "utils/FilterProgram.h"

namespace tgfx {
class CornerPinUniforms : public Uniforms {
 public:
  int positionHandle = -1;
  int textureCoordHandle = -1;
};

class CornerPinEffect : public RuntimeEffect {
 public:
  static std::shared_ptr<CornerPinEffect> Make(const Point& upperLeft, const Point& upperRight,
                                               const Point& lowerRight, const Point& lowerLeft);

  DEFINE_RUNTIME_EFFECT_PROGRAM_ID

  int sampleCount() const override {
    return 4;
  }

  Rect filterBounds(const Rect& srcRect, MapDirection) const override;

  std::unique_ptr<RuntimeProgram> onCreateProgram(Context* context) const override;

  bool onDraw(const RuntimeProgram* program, const std::vector<BackendTexture>& inputTextures,
              const BackendRenderTarget& target, const Point& offset) const override;

 private:
  Point cornerPoints[4] = {};
  float vertexQs[4] = {1.0f};

  CornerPinEffect(const Point& upperLeft, const Point& upperRight, const Point& lowerRight,
                  const Point& lowerLeft);

  std::vector<float> computeVertices(const BackendTexture& source,
                                     const BackendRenderTarget& target, const Point& offset) const;

  void calculateVertexQs();
};
}  // namespace tgfx
