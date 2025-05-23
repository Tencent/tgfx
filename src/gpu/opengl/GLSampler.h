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

#include "gpu/TextureSampler.h"
#include "tgfx/gpu/opengl/GLDefines.h"

namespace tgfx {
/**
 * Defines the sampling parameters for an OpenGL texture uint.
 */
class GLSampler : public TextureSampler {
 public:
  /**
   * The OpenGL texture id of the sampler.
   */
  unsigned id = 0;

  /**
   * The OpenGL texture target of the sampler.
   */
  unsigned target = GL_TEXTURE_2D;

  SamplerType type() const override;

  BackendTexture getBackendTexture(int width, int height) const override;

 protected:
  void computeKey(Context* context, BytesKey* bytesKey) const override;
};
}  // namespace tgfx
