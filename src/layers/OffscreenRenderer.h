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

#include <optional>
#include "layers/DrawArgs.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

class Canvas;
class Image;
class ImageFilter;
class Layer;
class Surface;

struct OffscreenResult {
  std::shared_ptr<Image> image = nullptr;
  Matrix imageMatrix = Matrix::I();

  bool isEmpty() const {
    return image == nullptr;
  }
};

class OffscreenRenderer {
 public:
  static OffscreenResult RenderContent(Layer* layer, const DrawArgs& args,
                                       const Matrix& contentMatrix,
                                       const std::optional<Rect>& clipBounds);

  static OffscreenResult RenderPassThrough(Layer* layer, const DrawArgs& args, Canvas* parentCanvas,
                                           const std::optional<Rect>& clipBounds);

 private:
  // Two backing variants for renderContent:
  //   - Surface: needed when a descendant Background-sourced style will readback through this
  //     subtree. Requires a GPU context.
  //   - Picture: default / fallback. Cheaper; readback (when present) goes through the Picture
  //     recorder's segment-flush mechanism.
  static OffscreenResult RenderContentOnSurface(
      Layer* layer, const DrawArgs& args, std::shared_ptr<Surface> surface, const Matrix& density,
      const Rect& imageClip, const std::shared_ptr<ImageFilter>& imageFilter,
      const std::optional<Rect>& clipBounds, const Rect& inputBounds, const Matrix& contentMatrix);
  static OffscreenResult RenderContentOnPicture(
      Layer* layer, const DrawArgs& args, const Matrix& density, const Rect& imageClip,
      const std::shared_ptr<ImageFilter>& imageFilter, const std::optional<Rect>& clipBounds,
      const Rect& inputBounds, const Matrix& contentMatrix, bool wantsSubBackground);

  // Two backing variants for renderPassThrough. Same split rationale as renderContent; both
  // seed the parent backdrop first so the pass-through subtree composes on top.
  static OffscreenResult RenderPassThroughOnSurface(Layer* layer, const DrawArgs& args,
                                                    std::shared_ptr<Surface> surface,
                                                    const std::shared_ptr<Image>& backdrop,
                                                    const Matrix& parentMatrix,
                                                    const Rect& surfaceRect,
                                                    const Rect& inputBounds);
  static OffscreenResult RenderPassThroughOnPicture(Layer* layer, const DrawArgs& args,
                                                    const std::shared_ptr<Image>& backdrop,
                                                    const Matrix& parentMatrix,
                                                    const Rect& surfaceRect,
                                                    const Rect& inputBounds);
};

}  // namespace tgfx
