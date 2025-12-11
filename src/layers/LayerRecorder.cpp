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

#include "tgfx/layers/LayerRecorder.h"
#include "layers/contents/ContourContent.h"
#include "layers/contents/DefaultContent.h"
#include "layers/contents/ForegroundContent.h"

namespace tgfx {
Canvas* LayerRecorder::getCanvas(LayerContentType contentType) {
  Canvas* canvas = nullptr;
  auto& recorder = recorders[static_cast<size_t>(contentType)];
  if (recorder == nullptr) {
    recorder = std::make_unique<PictureRecorder>();
    canvas = recorder->beginRecording();
  } else {
    canvas = recorder->getRecordingCanvas();
  }
  return canvas;
}

std::unique_ptr<LayerContent> LayerRecorder::finishRecording() {
  std::shared_ptr<Picture> defaultContent = nullptr;
  if (auto& defaultRecorder = recorders[static_cast<size_t>(LayerContentType::Default)]) {
    defaultContent = defaultRecorder->finishRecordingAsPicture();
  }
  std::shared_ptr<Picture> foreground = nullptr;
  if (auto& foregroundRecorder = recorders[static_cast<size_t>(LayerContentType::Foreground)]) {
    foreground = foregroundRecorder->finishRecordingAsPicture();
  }
  std::shared_ptr<Picture> contour = nullptr;
  if (auto& contourRecorder = recorders[static_cast<size_t>(LayerContentType::Contour)]) {
    contour = contourRecorder->finishRecordingAsPicture();
  }
  std::unique_ptr<LayerContent> layerContent = nullptr;
  if (foreground) {
    layerContent =
        std::make_unique<ForegroundContent>(std::move(defaultContent), std::move(foreground));
  } else if (defaultContent) {
    layerContent = std::make_unique<DefaultContent>(std::move(defaultContent));
  }
  if (contour && layerContent) {
    layerContent = std::make_unique<ContourContent>(std::move(layerContent), std::move(contour));
  }
  return layerContent;
}

}  // namespace tgfx