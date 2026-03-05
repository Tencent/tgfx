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
#include <chrono>
#include <cstdio>
#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#define PERF_LOG(fmt, ...) do { \
  char _buf[256]; \
  snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
  emscripten_console_warn(_buf); \
} while(0)
#else
#define PERF_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif
#include "core/utils/CopyPixels.h"
#include "core/utils/Log.h"
#include "gpu/proxies/GPUBufferProxy.h"

namespace tgfx {
SurfaceReadback::~SurfaceReadback() {
  DEBUG_ASSERT(!locked);
  if (flipYPixels != nullptr) {
    free(flipYPixels);
  }
}

bool SurfaceReadback::isReady(Context* context) const {
  if (context != proxy->getContext()) {
    LOGE("SurfaceReadback::isReady() Context mismatch!");
    return false;
  }
  auto readbackBuffer = proxy->getBuffer();
  if (readbackBuffer == nullptr) {
    return false;
  }
  return readbackBuffer->gpuBuffer()->isReady();
}

const void* SurfaceReadback::lockPixels(Context* context, bool flipY) {
  auto lockTotalStart = std::chrono::high_resolution_clock::now();

  if (context != proxy->getContext()) {
    LOGE("SurfaceReadback::lockPixels() Context mismatch!");
    return nullptr;
  }
  if (locked) {
    LOGE("SurfaceReadback::lockPixels() you must call unlockPixels() before locking again!");
    return nullptr;
  }

  auto flushStart = std::chrono::high_resolution_clock::now();
  auto readbackBuffer = proxy->getBuffer();
  bool didFlush = false;
  if (readbackBuffer == nullptr) {
    // The readback buffer is not created yet, we need to flush the context to create it.
    context->flushAndSubmit();
    didFlush = true;
    readbackBuffer = proxy->getBuffer();
    if (readbackBuffer == nullptr) {
      LOGE("SurfaceReadback::lockPixels() Failed to get readback buffer!");
      return nullptr;
    }
  }
  auto flushEnd = std::chrono::high_resolution_clock::now();
  double flushMs =
      std::chrono::duration<double, std::milli>(flushEnd - flushStart).count();

  auto mapStart = std::chrono::high_resolution_clock::now();
  auto pixels = readbackBuffer->gpuBuffer()->map();
  auto mapEnd = std::chrono::high_resolution_clock::now();
  double mapMs =
      std::chrono::duration<double, std::milli>(mapEnd - mapStart).count();

  double flipMs = 0.0;
  if (flipY) {
    auto flipStart = std::chrono::high_resolution_clock::now();
    flipYPixels = malloc(_info.byteSize());
    if (flipYPixels == nullptr) {
      readbackBuffer->gpuBuffer()->unmap();
      return nullptr;
    }
    CopyPixels(_info, pixels, _info, flipYPixels, true);
    pixels = flipYPixels;
    auto flipEnd = std::chrono::high_resolution_clock::now();
    flipMs = std::chrono::duration<double, std::milli>(flipEnd - flipStart).count();
  }
  locked = true;

  auto lockTotalEnd = std::chrono::high_resolution_clock::now();
  double lockTotalMs =
      std::chrono::duration<double, std::milli>(lockTotalEnd - lockTotalStart).count();

  PERF_LOG("[tgfx::lockPixels] %dx%d | flushAndSubmit(%s)=%.2fms gpuBuffer::map=%.2fms flipY(%s)=%.2fms | total=%.2fms",
         _info.width(), _info.height(),
         didFlush ? "yes" : "no", flushMs,
         mapMs,
         flipY ? "yes" : "no", flipMs,
         lockTotalMs);

  return pixels;
}

void SurfaceReadback::unlockPixels(Context* context) {
  if (context != proxy->getContext()) {
    LOGE("SurfaceReadback::unlockPixels() Context mismatch!");
    return;
  }
  if (!locked) {
    return;
  }
  locked = false;
  if (flipYPixels != nullptr) {
    free(flipYPixels);
    flipYPixels = nullptr;
  }
  auto readbackBuffer = proxy->getBuffer();
  DEBUG_ASSERT(readbackBuffer != nullptr);
  readbackBuffer->gpuBuffer()->unmap();
}
}  // namespace tgfx
