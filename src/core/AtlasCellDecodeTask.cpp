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

#include "AtlasCellDecodeTask.h"
#include "core/utils/ClearPixels.h"
#include "utils/Log.h"

namespace tgfx {
void AtlasCellDecodeTask::onExecute() {
  DEBUG_ASSERT(imageCodec != nullptr)
  ClearPixels(dstInfo, dstPixels);
  auto targetInfo = dstInfo.makeIntersect(0, 0, imageCodec->width(), imageCodec->height());
  auto targetPixels = dstInfo.computeOffset(dstPixels, padding, padding);
  imageCodec->readPixels(targetInfo, targetPixels);
  imageCodec = nullptr;
}

void AtlasCellDecodeTask::onCancel() {
  imageCodec = nullptr;
}
}  // namespace tgfx
