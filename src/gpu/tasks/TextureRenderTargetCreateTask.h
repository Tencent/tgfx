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

#include "ResourceTask.h"
#include "gpu/RenderTarget.h"

namespace tgfx {
class TextureRenderTargetCreateTask : public ResourceTask {
 public:
  TextureRenderTargetCreateTask(UniqueKey uniqueKey, int width, int height, PixelFormat format,
                                int sampleCount, bool mipmapped, ImageOrigin origin);

  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  int width = 0;
  int height = 0;
  PixelFormat format = PixelFormat::Unknown;
  int sampleCount = 1;
  bool mipmapped = false;
  ImageOrigin origin = ImageOrigin::TopLeft;
};
}  // namespace tgfx
