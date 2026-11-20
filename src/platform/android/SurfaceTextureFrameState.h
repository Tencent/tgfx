/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/platform/ImageReader.h"

namespace tgfx {
/** Latch state protected by SurfaceTexture's mutex, independent of JNI and GPU resources. */
class SurfaceTextureFrameState {
 public:
  void onSeeked() {
    hasFrame = false;
    latchAdvanced = false;
    timestamp = -1;
    seekPending = true;
  }

  void onLatched(int64_t value) {
    if (hasFrame && value < timestamp) {
      seekPending = false;
    }
    latchAdvanced = !hasFrame || value != timestamp;
    hasFrame = true;
    timestamp = value;
  }

  bool hasLatchedFrame() const {
    return hasFrame;
  }

  bool lastLatchAdvanced() const {
    return latchAdvanced;
  }

  int64_t lastLatchedTimestamp() const {
    return timestamp;
  }

  ImageReaderStatus readStatus(int64_t target, bool endOfRead = false) {
    if (!hasFrame) {
      return endOfRead ? ImageReaderStatus::NotReady : ImageReaderStatus::Pending;
    }
    if ((target >= 0 && timestamp == target) || (endOfRead && target < 0)) {
      seekPending = false;
      return ImageReaderStatus::Ready;
    }
    if (target >= 0 && timestamp > target && (!seekPending || endOfRead)) {
      // A completed mismatched read consumes the seek recovery allowance. Retrying that target
      // must not repeatedly drain the producer queue or wait for an unreachable timestamp.
      seekPending = false;
      return ImageReaderStatus::Mismatched;
    }
    return endOfRead ? ImageReaderStatus::NotReady : ImageReaderStatus::Pending;
  }

 private:
  bool hasFrame = false;
  bool latchAdvanced = false;
  int64_t timestamp = -1;
  bool seekPending = false;
};
}  // namespace tgfx
