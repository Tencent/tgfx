/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "GenerateMipmapsTask.h"
#include "tgfx/core/Clock.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
// Single-threaded WASM profiling accumulator. See TextureUploadTask.cpp for rationale.
static GenerateMipmapsTask::ProfileSnapshot gProfileSnapshot = {};

GenerateMipmapsTask::ProfileSnapshot GenerateMipmapsTask::FetchProfileAndReset() {
  auto snapshot = gProfileSnapshot;
  gProfileSnapshot = {};
  return snapshot;
}

GenerateMipmapsTask::GenerateMipmapsTask(BlockAllocator* allocator,
                                         std::shared_ptr<TextureProxy> textureProxy)
    : RenderTask(allocator), textureProxy(std::move(textureProxy)) {
}

void GenerateMipmapsTask::execute(CommandEncoder* encoder) {
  auto textureView = textureProxy->getTextureView();
  if (textureView == nullptr) {
    LOGE("GenerateMipmapsTask::execute() Failed to get texture view!");
    return;
  }
  int64_t startUs = Clock::Now();
  encoder->generateMipmapsForTexture(textureView->getTexture());
  int64_t endUs = Clock::Now();
  gProfileSnapshot.totalUs += (endUs - startUs);
  gProfileSnapshot.count++;
}
}  // namespace tgfx
