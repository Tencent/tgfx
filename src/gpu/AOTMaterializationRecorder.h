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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <vector>
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/AOTRenderPlan.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class Context;

/**
 * A blend-child materialization that has been decided but not rendered yet: the subtree processor
 * and the offscreen target its output goes to. The consumer already samples target through a
 * TextureEffect.
 */
struct AOTPendingProducer {
  MaterializedTarget target = {};
  float apronRadius = 0.0f;
  PlacementPtr<FragmentProcessor> processor = nullptr;
  uint32_t renderFlags = 0;
};

/**
 * Defers the blend-child materializations (FlattenToTexture) made while it is active, so a producer
 * shares the fate of the color chain that reads it. AOTRouter opens one around the materialization
 * retry's rebuild; then exactly one of three things happens to the recorded producers:
 *
 *   - The retry plan is taken: AOTPlanExecutor::Make moves the producers into the consumer's
 *     AOTRenderPlan (takeProducerPasses), so they run in the consumer's task, before it.
 *   - The retry is refused: the recorder is destroyed with the producers still pending and they
 *     are dropped without rendering (their target textures are still allocated, but never drawn).
 *   - Something enqueues a render task while producers are pending (for example a nested image
 *     filter fill built during the rebuild, which may sample them): DrawingManager first commits
 *     every pending producer as an ordinary offscreen fill (commitPending), so no task can read a
 *     producer before it renders.
 *
 * Recorders nest: constructing one installs it as the context's active recorder and destruction
 * restores the previous one. Producers are recorded in construction order, and a subtree is built
 * before the blend that consumes it, so a producer always comes after the producers it samples.
 */
class AOTMaterializationRecorder {
 public:
  explicit AOTMaterializationRecorder(Context* context);
  ~AOTMaterializationRecorder();

  AOTMaterializationRecorder(const AOTMaterializationRecorder&) = delete;
  AOTMaterializationRecorder& operator=(const AOTMaterializationRecorder&) = delete;

  bool empty() const {
    return producers.empty();
  }

  void record(AOTPendingProducer producer);

  /**
   * Turns every pending producer into AOTRenderPasses appended to passes, in recording order, and
   * clears the pending list. Each producer is a fill of its own target: a planned producer adds
   * its split-off passes followed by its terminal fill, an unplanned one adds a single fill with its
   * original processor. Returns false if a pass cannot be built; the producers are consumed either
   * way, which is only correct because a caller that fails here refuses the whole retry.
   */
  bool takeProducerPasses(std::vector<AOTRenderPass>* passes);

  /**
   * Enqueues every pending producer as an ordinary offscreen fill (DrawingManager::fillRTWithFP),
   * in recording order, and clears the pending list.
   */
  void commitPending();

 private:
  Context* context = nullptr;
  AOTMaterializationRecorder* previous = nullptr;
  std::vector<AOTPendingProducer> producers = {};

  friend class DrawingManager;
};
}  // namespace tgfx
