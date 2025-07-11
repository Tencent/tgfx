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
class BackendRenderTargetCreateTask : public ResourceTask {
 public:
  BackendRenderTargetCreateTask(UniqueKey uniqueKey, const BackendTexture& backendTexture,
                                int sampleCount, ImageOrigin origin, bool adopted);

  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  BackendTexture backendTexture = {};
  int sampleCount = 0;
  ImageOrigin origin = ImageOrigin::TopLeft;
  bool adopted = false;
};
}  // namespace tgfx
