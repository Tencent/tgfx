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

#include <array>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/PictureRecorder.h"

namespace tgfx {
class LayerContent;

/**
 * Defines the different types of content that can be recorded in a layer.
 */
enum class LayerContentType {
  /**
   * The default content of a layer, rendered beneath the layer’s children but above any layerStyles
   * positioned with LayerStylePosition::Below.
   */
  Default,

  /**
   * The foreground content of a layer, rendered above the layer’s children and all layerStyles.
   * This content also serves as part of the input source for layer styles. This content type is
   * optional.
   */
  Foreground,

  /**
   * The contour content of a layer, typically used for LayerMaskType::Contour masks or layerStyles
   * that require LayerStyleExtraSourceType::Contour. This content type is optional. If not
   * provided, the default and foreground content will be used as the contour instead.
   */
  Contour
};

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
  std::array<std::unique_ptr<PictureRecorder>, 3> recorders = {};

  std::unique_ptr<LayerContent> finishRecording();

  friend class Layer;
};
}  // namespace tgfx
