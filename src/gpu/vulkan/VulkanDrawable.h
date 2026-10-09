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

#include <memory>
#include "tgfx/gpu/Drawable.h"

namespace tgfx {

class VulkanWindow;

/**
 * A Drawable backed by a Vulkan swapchain frame. The frame handle is created without a Context;
 * its manual-present proxy (which acquires the swapchain image) is resolved when the frame is
 * imported into a Context. The frame stays readable between the render submission and
 * Context::present(); reading after the presentation is not supported because the presentation
 * engine owns the image once it has been presented.
 */
class VulkanDrawable : public Drawable {
 public:
  static std::shared_ptr<VulkanDrawable> Make(std::shared_ptr<VulkanWindow> window);

  ~VulkanDrawable() override;

 protected:
  std::shared_ptr<RenderTargetProxy> onImport(Context* context) override;
  bool onSchedulePresent(Context* context) override;
  void onPresent(Context* context) override;
  void onAbandon() override;

 private:
  explicit VulkanDrawable(std::shared_ptr<ColorSpace> colorSpace);
};

}  // namespace tgfx
