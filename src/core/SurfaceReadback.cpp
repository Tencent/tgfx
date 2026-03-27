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
#include <thread>
#include "core/utils/CopyPixels.h"
#include "core/utils/Log.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "tgfx/gpu/GPU.h"

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
  if (context != proxy->getContext()) {
    LOGE("SurfaceReadback::lockPixels() Context mismatch!");
    return nullptr;
  }
  if (locked) {
    LOGE("SurfaceReadback::lockPixels() you must call unlockPixels() before locking again!");
    return nullptr;
  }
  auto readbackBuffer = proxy->getBuffer();
  if (readbackBuffer == nullptr) {
    // The readback buffer has not been created yet because the transfer task has not been flushed.
    // Flush all pending drawing and transfer commands, then wait for GPU completion. We must pass
    // syncCpu=true because the GPU copy-to-buffer command needs to finish before we can map the
    // buffer and read valid pixel data on the CPU side.
    //
    // Why this is necessary across all backends:
    //   - Vulkan: submit() is async (fence-based), GPU may still be executing the transfer.
    //   - Metal:  [cmd commit] is async, GPU blit may not have finished yet.
    //   - OpenGL: glReadPixels is internally synchronous so this is redundant but harmless.
    //   - DX12:   ExecuteCommandLists is async, same as Vulkan — must fence before Map().
    context->flushAndSubmit(true);
    readbackBuffer = proxy->getBuffer();
    if (readbackBuffer == nullptr) {
      LOGE("SurfaceReadback::lockPixels() Failed to get readback buffer!");
      return nullptr;
    }
  } else {
    // The buffer already exists (transfer task was flushed in a previous submit), but under
    // asynchronous submission models the GPU transfer command may still be in-flight. We must
    // wait for all previously submitted GPU work to complete before mapping the buffer.
    //
    // Without this wait, map() would return a pointer to memory that the GPU has not yet written
    // to, resulting in stale or garbage pixel data (observed as test failures on Vulkan).
    context->gpu()->queue()->waitUntilCompleted();
  }
  auto gpuBuffer = readbackBuffer->gpuBuffer();
  // For async-only backends like WebGPU, trigger async mapping if not already started.
  // Without Asyncify, requestMapAsync() triggers wgpuBufferMapAsync with a callback.
  // The callback will be invoked later when the JS event loop processes it.
  if (!gpuBuffer->isReady()) {
    gpuBuffer->requestMapAsync();
    // Poll with a timeout, allowing some time for the callback to fire.
    // The callback will update mapReady flag when buffer mapping completes.
    const int POLL_TIMEOUT_MS = 5000;  // 5 second timeout
    const int POLL_INTERVAL_US = 1000;  // Poll every 1ms
    auto startTime = std::chrono::steady_clock::now();
    while (!gpuBuffer->isReady()) {
      auto elapsed = std::chrono::steady_clock::now() - startTime;
      if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > POLL_TIMEOUT_MS) {
        break;
      }
      // Without Asyncify, yield by sleeping a tiny bit. This is not ideal since it still blocks,
      // but some systems may give OS scheduler a chance to process async events.
      // Ideally this should use Asyncify or a fully async API redesign.
      std::this_thread::sleep_for(std::chrono::microseconds(POLL_INTERVAL_US));
    }
    if (!gpuBuffer->isReady()) {
      LOGE("SurfaceReadback::lockPixels() buffer mapping failed!");
      return nullptr;
    }
  }
  auto pixels = gpuBuffer->map();
  if (pixels == nullptr) {
    return nullptr;
  }
  if (flipY) {
    flipYPixels = malloc(_info.byteSize());
    if (flipYPixels == nullptr) {
      readbackBuffer->gpuBuffer()->unmap();
      return nullptr;
    }
    CopyPixels(_info, pixels, _info, flipYPixels, true);
    pixels = flipYPixels;
  }
  locked = true;
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
