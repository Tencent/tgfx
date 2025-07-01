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

#include "AtlasCellCodecTask.h"
#include "core/utils/ClearPixels.h"

namespace tgfx {

void AtlasCellCodecTask::onExecute() {
  if (imageCodec == nullptr) {
    return;
  }
  ClearPixels(dstInfo, dstPixels);
  auto offsetPixels = static_cast<uint8_t*>(dstPixels) +
                      (dstInfo.rowBytes() + dstInfo.bytesPerPixel()) * static_cast<size_t>(padding);
  auto info = dstInfo.makeIntersect(0, 0, imageCodec->width(), imageCodec->height());
  imageCodec->readPixels(info, offsetPixels);
}

void AtlasCellCodecTask::onCancel() {
  imageCodec = nullptr;
}
}  // namespace tgfx
