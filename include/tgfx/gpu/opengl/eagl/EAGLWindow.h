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

#include "EAGLDevice.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {
class EAGLLayerTexture;

class EAGLWindow : public Window {
 public:
  /**
   * Creates a new window from a CAEAGLLayer with the specified device.
   */
  static std::shared_ptr<EAGLWindow> MakeFrom(CAEAGLLayer* layer,
                                              std::shared_ptr<GLDevice> device = nullptr,
                                              std::shared_ptr<ColorSpace> colorSpace = nullptr);

 protected:
  std::shared_ptr<Surface> onCreateSurface(Context* context) override;
  void onPresent(Context* context) override;

 private:
  CAEAGLLayer* layer = nil;
  std::shared_ptr<EAGLLayerTexture> layerTexture;
  std::shared_ptr<ColorSpace> colorSpace;

  EAGLWindow(std::shared_ptr<Device> device, CAEAGLLayer* layer,
             std::shared_ptr<ColorSpace> colorSpace = nullptr);
};
}  // namespace tgfx
