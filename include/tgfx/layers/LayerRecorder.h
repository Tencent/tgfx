/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <array>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/layers/LayerContent.h"

namespace tgfx {
class RecordedContent;

/**
 * LayerRecorder is a utility class that records drawing commands as layer content.
 */
class LayerRecorder {
 public:
  /**
   * Returns a Canvas for recording drawing commands. The content type determines where the recorded
   * commands will be stored:
   * - Default: The commands will be stored in the default content of the layer.
   * - Foreground: The commands will be stored in the foreground content of the layer.
   * - Contour: The commands will be stored in the contour content of the layer.
   * If the content type is not specified, it defaults to ContentType::Default.
   */
  Canvas* getCanvas(LayerContentType contentType = LayerContentType::Default);

 private:
  std::array<std::unique_ptr<Recorder>, 3> recorders = {};

  std::unique_ptr<RecordedContent> finishRecording();

  friend class Layer;
};
}  // namespace tgfx
