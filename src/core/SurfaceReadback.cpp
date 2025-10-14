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

#include "tgfx/core/SurfaceReadback.h"
#include "core/utils/CopyPixels.h"
#include "core/utils/Log.h"
#include "gpu/proxies/GPUBufferProxy.h"

namespace tgfx {
SurfaceReadback::~SurfaceReadback() {
  if (flipYPixels != nullptr) {
    free(flipYPixels);
  }
}

bool SurfaceReadback::isReady(Context* context) const {
  if (!checkContext(context)) {
    return false;
  }
  auto readbackBuffer = proxy->getBuffer();
  if (readbackBuffer == nullptr) {
    return false;
  }
  return readbackBuffer->gpuBuffer()->isReady();
}

const void* SurfaceReadback::lockPixels(Context* context, bool flipY) {
  if (!checkContext(context)) {
    return nullptr;
  }
  if (flipYPixels != nullptr) {
    LOGE("SurfaceReadback::lockPixels() you must call unlockPixels() before locking again!");
    return nullptr;
  }
  auto readbackBuffer = proxy->getBuffer();
  if (readbackBuffer == nullptr) {
    // The readback buffer is not created yet, we need to flush the context to create it.
    context->flushAndSubmit();
    readbackBuffer = proxy->getBuffer();
    if (readbackBuffer == nullptr) {
      LOGE("SurfaceReadback::lockPixels() Failed to get readback buffer!");
      return nullptr;
    }
  }
  auto pixels = readbackBuffer->gpuBuffer()->map();
  if (!flipY) {
    return pixels;
  }
  flipYPixels = malloc(_info.byteSize());
  if (flipYPixels == nullptr) {
    readbackBuffer->gpuBuffer()->unmap();
    return nullptr;
  }
  CopyPixels(_info, pixels, _info, flipYPixels, true);
  return flipYPixels;
}

void SurfaceReadback::unlockPixels(Context* context) {
  if (!checkContext(context)) {
    return;
  }
  if (flipYPixels != nullptr) {
    free(flipYPixels);
    flipYPixels = nullptr;
  }
  auto readbackBuffer = proxy->getBuffer();
  if (readbackBuffer == nullptr) {
    LOGE("SurfaceReadback::unlockPixels() Readback buffer is null!");
    return;
  }
  readbackBuffer->gpuBuffer()->unmap();
}

bool SurfaceReadback::checkContext(Context* context) const {
  if (context != proxy->getContext()) {
    LOGE("SurfaceReadback::isReady() Context mismatch!");
    return false;
  }
  return true;
}
}  // namespace tgfx
