/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/ImageSource.h"
#include "core/PixelBuffer.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
class TextAtlasUploadTask : public ResourceTask {
 public:
  TextAtlasUploadTask(UniqueKey uniqueKey, std::shared_ptr<DataSource<PixelBuffer>> source,
                      std::shared_ptr<TextureProxy> proxy, Point atlasOffset);
  bool execute(Context* context) override;

 protected:
  std::shared_ptr<Resource> onMakeResource(Context*) override {
    // The execute() method is already overridden, so this method should never be called.
    return nullptr;
  }

 private:
  std::shared_ptr<DataSource<PixelBuffer>> source = nullptr;
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  Point atlasOffset = Point::Zero();
};
}  // namespace tgfx
