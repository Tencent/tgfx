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

#include <atomic>
#include "tgfx/layers/LayerImageReader.h"

namespace tgfx {

/**
 * Cancellation and failure state shared by one strict export draw. The tiled export passes it
 * through DrawArgs::drawControl so that layer drawing reports failures instead of degrading the
 * output. The regular display drawing path leaves DrawArgs::drawControl null and keeps its
 * original silent fallback behavior.
 */
struct DrawControl {
  const std::atomic<bool>* cancelRequested = nullptr;
  LayerImageErrorCode error = LayerImageErrorCode::None;

  /**
   * Returns true if cancellation was requested. A null cancelRequested means the draw cannot be
   * cancelled.
   */
  bool cancelled() const {
    return cancelRequested != nullptr && cancelRequested->load();
  }

  /**
   * Returns true if a failure has been recorded.
   */
  bool failed() const {
    return error != LayerImageErrorCode::None;
  }

  /**
   * Records a failure. Only the first error code is kept; later calls are ignored.
   */
  void fail(LayerImageErrorCode code) {
    if (!failed()) {
      error = code;
    }
  }
};

}  // namespace tgfx
