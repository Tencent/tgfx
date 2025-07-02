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

#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Task.h"

namespace tgfx {
class AtlasCellDecodeTask final : public Task {
 public:
  AtlasCellDecodeTask(std::shared_ptr<ImageCodec> imageCodec, void* dstPixels,
                      const ImageInfo& dstInfo, int padding)
      : imageCodec(std::move(imageCodec)), dstPixels(dstPixels), dstInfo(dstInfo),
        padding(padding) {
  }

 protected:
  void onExecute() override;

  void onCancel() override;

 private:
  std::shared_ptr<ImageCodec> imageCodec = nullptr;
  void* dstPixels = nullptr;
  ImageInfo dstInfo = {};
  int padding = 0;
};
}  // namespace tgfx
