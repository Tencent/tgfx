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

#include "tgfx/platform/HardwareBuffer.h"
#include "platform/web/TGFXWasmBindings.h"
#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
// Force TGFXBindInit to be linked.
static auto bind = TGFXBindInit();

std::shared_ptr<ImageBuffer> ImageBuffer::MakeFrom(HardwareBufferRef, YUVColorSpace) {
  return nullptr;
}

bool HardwareBufferCheck(HardwareBufferRef) {
  return false;
}

HardwareBufferRef HardwareBufferAllocate(int, int, bool) {
  return nullptr;
}

HardwareBufferRef HardwareBufferRetain(HardwareBufferRef buffer) {
  return buffer;
}

void HardwareBufferRelease(HardwareBufferRef) {
}

void* HardwareBufferLock(HardwareBufferRef) {
  return nullptr;
}

void HardwareBufferUnlock(HardwareBufferRef) {
}

ImageInfo HardwareBufferGetInfo(HardwareBufferRef) {
  return {};
}
}  // namespace tgfx
