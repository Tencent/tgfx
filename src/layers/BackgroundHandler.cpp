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

#include "BackgroundHandler.h"
#include "core/utils/Log.h"
#include "layers/BackgroundSnapshotMap.h"
#include "layers/BackgroundSource.h"
#include "layers/DrawArgs.h"
#include "layers/LayerStyleSource.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

namespace {

// A handler that does nothing for background-sourced styles. Reused for intermediate artifacts
// and 3D / contour paths where background styles must not produce output.
class NoOpImpl : public BackgroundHandler {
 public:
  void drawBackgroundStyle(const DrawArgs& /*args*/, Canvas* /*canvas*/, Layer* /*layer*/,
                           float /*alpha*/, LayerStyle* /*style*/,
                           const LayerStyleSource* /*source*/) override {
    // Background styles deliberately produce no output in NoOp contexts.
  }
};

}  // namespace

BackgroundHandler* BackgroundHandler::NoOp() {
  static NoOpImpl instance;
  return &instance;
}

void BackgroundHandler::DispatchOrSkip(const DrawArgs& args, Canvas* canvas, Layer* layer,
                                       float alpha, LayerStyle* style,
                                       const LayerStyleSource* source) {
  auto* handler = args.background.handler ? args.background.handler : BackgroundHandler::NoOp();
  handler->drawBackgroundStyle(args, canvas, layer, alpha, style, source);
}

void BackgroundCapturer::drawBackgroundStyle(const DrawArgs& args, Canvas* /*canvas*/, Layer* layer,
                                             float /*alpha*/, LayerStyle* style,
                                             const LayerStyleSource* source) {
  if (snapshots == nullptr || bgSource == nullptr) {
    return;
  }
  // The consumer computes source->contentScale from `canvas.matrix.getMaxScale()` on the final
  // render canvas. The capturer walks the tree on the bg canvas whose matrix is
  // `viewMatrix * surfaceScale`, so the LayerStyleSource built during capture has contentScale
  // reduced by surfaceScale. Divide it out so the snapshot lands at the consumer's expected scale.
  auto surfaceScale = bgSource->surfaceScale();
  auto contentScale = source != nullptr ? source->contentScale : baseCaptureScale;
  if (!FloatNearlyZero(surfaceScale) && surfaceScale != 1.0f) {
    contentScale /= surfaceScale;
  }
  if (FloatNearlyZero(contentScale)) {
    return;
  }
  auto bounds = layer->getBounds();
  bounds.scale(contentScale, contentScale);
  bounds.roundOut();
  auto localToGlobal = layer->getGlobalMatrix().asMatrix();
  Matrix globalToLocal = Matrix::I();
  if (!localToGlobal.invert(&globalToLocal)) {
    return;
  }
  auto bgImage = bgSource->getBackgroundImage();
  if (bgImage == nullptr) {
    return;
  }
  PictureRecorder recorder = {};
  auto* recording = recorder.beginRecording();
  recording->scale(contentScale, contentScale);
  recording->concat(globalToLocal);
  recording->concat(bgSource->backgroundMatrix());
  recording->drawImage(bgImage);
  auto picture = recorder.finishRecordingAsPicture();
  if (picture == nullptr) {
    return;
  }
  Point offset = {};
  auto image = ToImageWithOffset(std::move(picture), &offset, &bounds, args.dstColorSpace);
  if (image == nullptr) {
    return;
  }
  snapshots->emplace(BackgroundSnapshotKey{layer, style},
                     BackgroundSnapshotEntry{std::move(image), offset});
}

std::unique_ptr<BackgroundHandler> BackgroundCapturer::cloneWithSource(
    std::shared_ptr<BackgroundSource> newSource) const {
  if (newSource == nullptr) {
    return nullptr;
  }
  return std::unique_ptr<BackgroundHandler>(
      new BackgroundCapturer(snapshots, baseCaptureScale, std::move(newSource)));
}

void BackgroundConsumer::drawBackgroundStyle(const DrawArgs& /*args*/, Canvas* canvas, Layer* layer,
                                             float alpha, LayerStyle* style,
                                             const LayerStyleSource* source) {
  if (snapshots == nullptr || source == nullptr || FloatNearlyZero(source->contentScale)) {
    return;
  }
  auto it = snapshots->find(BackgroundSnapshotKey{layer, style});
  if (it == snapshots->end()) {
    return;
  }
  const auto& snapshot = it->second;
  auto groupIndex = static_cast<int>(style->excludeChildEffects());
  auto* group = source->groups[groupIndex].get();
  if (group == nullptr) {
    return;
  }
  const auto& contentEntry = group->content;
  AutoCanvasRestore restoreCanvas(canvas);
  auto matrix = Matrix::MakeScale(1.f / source->contentScale, 1.f / source->contentScale);
  matrix.preTranslate(contentEntry.offset.x, contentEntry.offset.y);
  canvas->concat(matrix);
  auto backgroundOffset = snapshot.offset - contentEntry.offset;
  style->drawWithExtraSource(canvas, contentEntry.image, source->contentScale, snapshot.image,
                             backgroundOffset, alpha);
}

void BackgroundCapturer::Run(Layer* captureRoot, const DrawArgs& baseArgs,
                             std::shared_ptr<BackgroundSource> bgSource, float captureScale,
                             BackgroundSnapshotMap* snapshots) {
  DEBUG_ASSERT(captureRoot != nullptr);
  DEBUG_ASSERT(bgSource != nullptr);
  DEBUG_ASSERT(snapshots != nullptr);
  auto* bgCanvas = bgSource->getCanvas();
  AutoCanvasRestore autoRestore(bgCanvas);
  BackgroundCapturer capturer(snapshots, captureScale, std::move(bgSource));
  DrawArgs captureArgs = baseArgs;
  captureArgs.background.handler = &capturer;
  captureRoot->drawLayer(captureArgs, bgCanvas, 1.0f, BlendMode::SrcOver);
}

void BackgroundState::resetToIntermediateArtifact() {
  handler = BackgroundHandler::NoOp();
}

}  // namespace tgfx
