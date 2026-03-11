/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/gpu/Drawable.h"

@class CAEAGLLayer;

namespace tgfx {
class EAGLLayerTexture;

class EAGLDrawable : public Drawable {
 public:
  EAGLDrawable(CAEAGLLayer* layer, int width, int height,
               std::shared_ptr<ColorSpace> colorSpace = nullptr);

 protected:
  std::shared_ptr<RenderTargetProxy> getProxy(Context* context) override;
  void onPresent(Context* context) override;

 private:
  CAEAGLLayer* layer = nil;
  std::shared_ptr<EAGLLayerTexture> layerTexture = nullptr;
};
}  // namespace tgfx
