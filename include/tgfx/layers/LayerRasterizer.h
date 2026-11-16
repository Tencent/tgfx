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

#include <cstddef>
#include <functional>
#include <memory>
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Size.h"

namespace tgfx {
class Context;
class Layer;

enum class RasterizeResult {
  Success,
  InvalidInput,
  UnsupportedTransform,
  DependencyTooLarge,
  AllocationFailed,
  RenderFailed,
  Cancelled
};

struct LayerRasterizeOptions {
  ISize size = {};
  // Read a band of the complete image without changing its coordinate system or scale.
  // A zero rowCount reads through size.height. Callback y values remain image-global.
  int firstRow = 0;
  int rowCount = 0;
  // Target layer-local coordinates to the complete output image. Never quantized per tile.
  Matrix matrix = Matrix::I();
  Color clearColor = Color::Transparent();
  std::shared_ptr<ColorSpace> renderColorSpace = ColorSpace::SRGB();
  std::shared_ptr<ColorSpace> outputColorSpace = ColorSpace::SRGB();
  // Upper bounds, additionally clamped to the device limit. No change to output scale.
  int maxTileSize = 0;  // 0 uses the device limit; a positive value can impose a smaller limit.
  size_t maxSurfacePixels = 16U * 1024U * 1024U;
  size_t maxRowBufferBytes = 32U * 1024U * 1024U;
};

class LayerRasterizer {
 public:
  // Renders the target subtree while retaining its root/ancestor/sibling backdrop context.
  // The target must be attached to a DisplayList. Like Layer::draw, matrix supplies the target's
  // transform and target alpha is applied; callers decide the export window and clipping policy.
  // Calls onRows in ascending y order with full-width, premultiplied RGBA rows. Pixel memory is
  // valid only during the callback. A false callback cancels; callers must discard partial output
  // on any non-Success result. This method and its callback run on the context's owning thread.
  // Rasterization preserves density; unsupported dependency footprints fail instead of scaling.
  // This entry point currently supports affine 2D trees. Perspective/preserve-3D content returns
  // UnsupportedTransform before emitting rows; ordinary Layer::draw remains available for it.
  static RasterizeResult ReadRows(Context* context, const std::shared_ptr<Layer>& target,
                                  const LayerRasterizeOptions& options,
                                  const std::function<bool(int y, const Pixmap& rows)>& onRows);

 private:
  struct Impl;
};
}  // namespace tgfx
