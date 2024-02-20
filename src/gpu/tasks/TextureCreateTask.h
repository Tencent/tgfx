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
#include "core/ImageDecoder.h"
#include "tgfx/gpu/ImageOrigin.h"

namespace tgfx {
class TextureCreateTask : public ResourceTask {
 public:
  /**
   * Creates a TextureCreateTask to generate a texture using the specified size and format.
   */
  static std::shared_ptr<TextureCreateTask> MakeFrom(UniqueKey uniqueKey, int width, int height,
                                                     PixelFormat format, bool mipmapped = false,
                                                     ImageOrigin origin = ImageOrigin::TopLeft);

  /*
   * Creates a TextureCreateTask to generate a texture using the given ImageBuffer.
   */
  static std::shared_ptr<TextureCreateTask> MakeFrom(UniqueKey uniqueKey,
                                                     std::shared_ptr<ImageDecoder> imageDecoder,
                                                     bool mipmapped = false);

 protected:
  explicit TextureCreateTask(UniqueKey uniqueKey) : ResourceTask(std::move(uniqueKey)) {
  }
};
}  // namespace tgfx
