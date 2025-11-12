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

#import <UIKit/UIKit.h>
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
class EAGLLayerTexture : public GLTexture {
 public:
  static std::shared_ptr<EAGLLayerTexture> MakeFrom(GLGPU* gpu, CAEAGLLayer* layer);

  unsigned frameBufferID() const override {
    return _frameBufferID;
  }

  unsigned colorBufferID() const {
    return renderBufferID;
  }

  void release(GLGPU* gpu) {
    onRelease(gpu);
  }

 protected:
  void onReleaseTexture(GLGPU* gpu) override;

 private:
  unsigned _frameBufferID = 0;
  unsigned renderBufferID = 0;

  EAGLLayerTexture(const TextureDescriptor& descriptor, unsigned frameBufferID)
      : GLTexture(descriptor, GL_TEXTURE_2D, 0), _frameBufferID(frameBufferID) {
  }

  friend class GLGPU;
};
}  // namespace tgfx
