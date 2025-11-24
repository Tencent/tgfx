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

#pragma once

#include <cstdint>

namespace tgfx {
/**
 * Recording represents a snapshot of rendering commands that have been flushed from a Context but
 * not yet submitted to the GPU. This allows for deferred submission, giving applications control
 * over when GPU work is actually queued for execution. A Recording can be submitted to the GPU via
 * Context::submit().
 * Note: If multiple Recording objects are created, submitting a later Recording will force all
 * earlier Recordings to be submitted first, maintaining the correct rendering order.
 */
class Recording {
 private:
  Recording(uint32_t contextID, uint32_t drawingBufferID, uint64_t generation)
      : contextID(contextID), drawingBufferID(drawingBufferID), generation(generation) {
  }

  uint32_t contextID = 0;
  uint32_t drawingBufferID = 0;
  uint64_t generation = 0;

  friend class Context;
};
}  // namespace tgfx
