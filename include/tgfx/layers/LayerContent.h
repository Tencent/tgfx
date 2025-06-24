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

namespace tgfx {
class LayerRecorder;

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
 * LayerContent represents the deferred contents of a layer, which may include default content,
 * foreground content, and the layer's contour. It delays computing the layer's contents until they
 * are actually needed. LayerContent must be immutable and cannot be modified after creation to
 * ensure thread safety.
 */
class LayerContent {
 public:
  virtual ~LayerContent() = default;

  /**
   * Draws the contents of the layer. Subclasses should override this method to record the layer’s
   * contents, typically by drawing on a canvas obtained from the provided LayerRecorder. This
   * method is similar to Layer::onUpdateContent(), but may be called on a background thread. Ensure
   * all operations here are thread-safe and do not depend on main thread state.
   * @param recorder The LayerRecorder used to record the layer's contents.
   */
  virtual void onDrawContent(LayerRecorder* recorder) const = 0;
};

}  // namespace tgfx
