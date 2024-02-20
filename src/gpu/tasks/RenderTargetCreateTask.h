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

#include "ResourceTask.h"

namespace tgfx {
class RenderTargetCreateTask : public ResourceTask {
 public:
  /**
   * Create a new RenderTargetCreateTask to generate a RenderTarget with the given properties.
   */
  static std::shared_ptr<RenderTargetCreateTask> MakeFrom(UniqueKey uniqueKey, UniqueKey textureKey,
                                                          PixelFormat pixelFormat,
                                                          int sampleCount = 1);

 protected:
  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  UniqueKey textureKey = {};
  PixelFormat pixelFormat = PixelFormat::RGBA_8888;
  int sampleCount = 1;

  RenderTargetCreateTask(UniqueKey uniqueKey, UniqueKey textureKey, PixelFormat pixelFormat,
                         int sampleCount);
};
}  // namespace tgfx
