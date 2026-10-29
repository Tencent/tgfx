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

#include "AOTMaterializationRecorder.h"
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/AOTPlanExecutor.h"
#include "gpu/DrawingManager.h"
#include "gpu/PrecompiledShaderCache.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

AOTMaterializationRecorder::AOTMaterializationRecorder(Context* context) : context(context) {
  auto drawingManager = context->drawingManager();
  previous = drawingManager->activeRecorder;
  drawingManager->activeRecorder = this;
}

AOTMaterializationRecorder::~AOTMaterializationRecorder() {
  // Producers still pending here belong to a refused retry: nothing samples their targets, so they
  // are dropped without rendering.
  auto drawingManager = context->drawingManager();
  DEBUG_ASSERT(drawingManager->activeRecorder == this);
  drawingManager->activeRecorder = previous;
}

void AOTMaterializationRecorder::record(AOTPendingProducer producer) {
  producers.push_back(std::move(producer));
}

bool AOTMaterializationRecorder::takeProducerPasses(std::vector<AOTRenderPass>* passes) {
  auto pending = std::move(producers);
  producers.clear();
  auto drawingManager = context->drawingManager();
  for (auto& producer : pending) {
    const auto& target = producer.target.renderTarget;
    const auto& geometry = producer.target.geometry;
    // Plan the producer exactly as DrawingManager::fillRTWithFP plans an offscreen fill, so the
    // producer is served the same way it would be as a fill task of its own.
    AOTEffectGraph graph = {};
    AOTEffectPlan plan = {};
    OffscreenFillKey diagnosticKey = InvalidOffscreenFillKey;
    bool planned =
        drawingManager->planFill(producer.processor.get(), geometry.coordOffset,
                                 OffscreenFillSource::FPFlatten, &graph, &plan, &diagnosticKey);
    auto fillProcessor = std::move(producer.processor);
    if (planned) {
      // A planned producer contributes its split-off passes first, then a fill with its terminal
      // color processor, which is what its own plan task would replay over the fill draw.
      DrawOp::ColorProcessorList terminalColors = {};
      auto deviceBounds =
          Rect::MakeWH(static_cast<float>(target->width()), static_cast<float>(target->height()));
      if (!AOTPlanExecutor::BuildPasses(context, producer.renderFlags, graph, plan, deviceBounds,
                                        target.get(), geometry.coordOffset, std::nullopt, passes,
                                        &terminalColors) ||
          terminalColors.size() != 1) {
        return false;
      }
      fillProcessor = std::move(terminalColors[0]);
    }
    AOTRenderPass pass = {};
    pass.target = target;
    pass.drawOp =
        drawingManager->makeFillDrawOp(target, std::move(fillProcessor), producer.renderFlags,
                                       geometry.coordOffset, diagnosticKey);
    if (pass.drawOp == nullptr) {
      return false;
    }
    pass.edge = AOTMaterializedEdge{AOTEdgeSpace::Local, AOTEdgeReason::OperandMaterialize,
                                    geometry, producer.apronRadius};
    passes->push_back(std::move(pass));
  }
  return true;
}

void AOTMaterializationRecorder::commitPending() {
  // Detach the list first: each fill below enqueues a render task, which asks this recorder to
  // commit again.
  auto pending = std::move(producers);
  producers.clear();
  auto drawingManager = context->drawingManager();
  auto cache = context->precompiledShaderCache();
  for (auto& producer : pending) {
    const auto& geometry = producer.target.geometry;
    if (!drawingManager->fillRTWithFP(producer.target.renderTarget, std::move(producer.processor),
                                      producer.renderFlags, geometry.coordOffset,
                                      OffscreenFillSource::FPFlatten)) {
      // Only reachable with a null target or processor, which record() never receives.
      LOGE("AOTMaterializationRecorder::commitPending() Failed to enqueue a producer fill!");
      continue;
    }
    if (cache != nullptr && cache->diagnosticRecordingEnabled()) {
      cache->recordMaterializedEdge(geometry.byteSize());
    }
  }
}
}  // namespace tgfx
