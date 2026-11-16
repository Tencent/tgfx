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

#include "tgfx/layers/LayerRasterizer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
#include <vector>
#include "core/Matrix3DUtils.h"
#include "layers/BackgroundHandler.h"
#include "layers/BackgroundSource.h"
#include "layers/DrawArgs.h"
#include "layers/RootLayer.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/filters/LayerFilter.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {
namespace {
bool Finite(const Rect& rect) {
  return std::isfinite(rect.left) && std::isfinite(rect.top) && std::isfinite(rect.right) &&
         std::isfinite(rect.bottom);
}
bool NeedsBackground(const LayerStyle* style) {
  return (style->extraSourceType() &
          static_cast<uint32_t>(LayerStyleExtraSourceType::Background)) != 0;
}

struct Requirements {
  Rect work = {};      // All pixel regions needed by this subtree.
  Rect backdrop = {};  // Only dependencies on earlier paint operations.
};

// Reverse the conservative output outsets of a style. Styles combine in parallel; unlike a
// filter chain their input outsets must not be summed. Include direct content as well.
Rect StyleInput(const Rect& output, const Rect& mappedProbe) {
  auto input = Rect::MakeLTRB(output.left - std::max(0.f, mappedProbe.right - 1.f),
                              output.top - std::max(0.f, mappedProbe.bottom - 1.f),
                              output.right + std::max(0.f, -mappedProbe.left),
                              output.bottom + std::max(0.f, -mappedProbe.top));
  return input;
}
}  // namespace

struct LayerRasterizer::Impl {
  Context* context;
  Layer* target;
  Layer* root;
  const LayerRasterizeOptions& options;
  Matrix rootToOutput;
  int textureLimit;
  bool needsBackground;
  RasterizeResult error = RasterizeResult::Success;
  bool oversizedDependency = false;

  // Validate once, before delivering any rows. A tiled 2D projection must not accidentally
  // flatten a preserve-3D subtree or change a perspective transform's sampling per tile.
  static bool SupportsTree(const Layer* layer) {
    const auto matrix = layer->matrix3D();
    if (layer->canPreserve3D() || !Matrix3DUtils::IsMatrix3DAffine(matrix) ||
        !matrix.asMatrix().isFinite())
      return false;
    for (const auto& child : layer->_children) {
      if (child->visible() && child->_alpha > 0 && !SupportsTree(child.get())) return false;
    }
    return !layer->_mask || SupportsTree(layer->_mask.get());
  }

  bool fits(const Rect& rect) const {
    const auto textureSize = static_cast<float>(textureLimit);
    return Finite(rect) && !rect.isEmpty() && rect.width() <= textureSize &&
           rect.height() <= textureSize &&
           static_cast<double>(rect.width()) * rect.height() <=
               static_cast<double>(options.maxSurfacePixels);
  }

  // Work backwards through paint order. Filters propagate their requested input to descendants;
  // only backdrop dependencies propagate to earlier siblings. Independent siblings/styles union
  // rectangles instead of adding their radii to a scene-wide symmetric halo.
  Requirements requirements(Layer* layer, const Matrix& matrix, const Rect& requested) {
    Requirements result;
    if (!matrix.isFinite() || matrix.hasPerspective() || layer->canPreserve3D()) {
      error = RasterizeResult::UnsupportedTransform;
      return result;
    }
    Matrix inverse;
    if (!matrix.invert(&inverse)) return result;  // A collapsed layer contributes no pixels.
    auto visible = matrix.mapRect(layer->getBounds(layer));
    if (!Finite(visible)) {
      error = RasterizeResult::InvalidInput;
      return result;
    }
    if (!visible.intersect(requested)) return result;
    auto density = matrix.getMaxScale();
    if (!std::isfinite(density) || density <= 0) return result;
    auto localOutput = inverse.mapRect(visible);
    localOutput.scale(density, density);
    auto input = layer->mapOutputBoundsToInput(localOutput, density);
    if (!Finite(input)) {
      error = RasterizeResult::InvalidInput;
      return result;
    }
    // Layer filters rasterize at isotropic content density, which may be much larger than the
    // final device footprint under non-uniform transforms. Budget this intermediate separately.
    if (!layer->_filters.empty()) {
      auto rasterInput = input;
      rasterInput.roundOut();
      if (!fits(rasterInput)) oversizedDependency = true;
    }
    auto contentInput = input;
    for (const auto& style : layer->_layerStyles) {
      const auto probe = style->filterBounds(Rect::MakeWH(1, 1), density);
      if (!Finite(probe)) {
        error = RasterizeResult::InvalidInput;
        return result;
      }
      contentInput.join(StyleInput(input, probe));
    }
    auto toOutput = [&](Rect rect) {
      rect.scale(1.f / density, 1.f / density);
      return matrix.mapRect(rect);
    };
    auto demand = toOutput(contentInput);
    result.work = demand;
    auto includeStyles = [&](LayerStylePosition position) {
      // Each style samples the same base input. Preserve the true union, including direction.
      auto backdrop = Rect::MakeEmpty();
      for (const auto& style : layer->_layerStyles) {
        if (style->position() != position || !NeedsBackground(style.get())) continue;
        auto required = style->filterBackground(contentInput, density);
        if (!Finite(required)) {
          error = RasterizeResult::InvalidInput;
          continue;
        }
        backdrop.join(toOutput(required));
      }
      demand.join(backdrop);
      result.backdrop.join(backdrop);
      result.work.join(backdrop);
    };
    includeStyles(LayerStylePosition::Above);
    for (auto it = layer->_children.rbegin(); it != layer->_children.rend(); ++it) {
      auto* child = it->get();
      if (!child->visible() || child->_alpha <= 0 || child->maskOwner) continue;
      auto childMatrix = matrix;
      auto childTransform = child->getMatrixWithScrollRect();
      childMatrix.preConcat(childTransform.asMatrix());
      auto needed = requirements(child, childMatrix, demand);
      result.work.join(needed.work);
      result.backdrop.join(needed.backdrop);
      demand.join(needed.backdrop);
      if (error != RasterizeResult::Success) return result;
    }
    includeStyles(LayerStylePosition::Below);
    return result;
  }

  bool plan(const Rect& core, Rect* drawRect, Rect* captureRect) {
    // Keep rasterization/AA sampling away from the delivery edge. The translation remains an
    // integer offset in the single global output grid, including at the outside image boundary.
    *drawRect = core.makeOutset(1, 1);
    drawRect->roundOut();
    if (!fits(*drawRect)) return false;
    *captureRect = *drawRect;
    oversizedDependency = false;
    auto needed = requirements(needsBackground ? root : target,
                               needsBackground ? rootToOutput : options.matrix, *drawRect);
    if (error != RasterizeResult::Success) return false;
    if (oversizedDependency) return false;
    captureRect->join(needed.work);
    captureRect->outset(1, 1);
    captureRect->roundOut();
    return fits(*captureRect);
  }

  bool draw(const Rect& core, const Rect& drawRect, const Rect& captureRect, uint8_t* pixels,
            size_t rowBytes) {
    auto surface = Surface::Make(context, static_cast<int>(drawRect.width()),
                                 static_cast<int>(drawRect.height()), ColorType::RGBA_8888, 1, 0, 0,
                                 options.renderColorSpace);
    if (!surface) {
      error = RasterizeResult::AllocationFailed;
      return false;
    }
    RasterizationState state = {false, textureLimit, options.maxSurfacePixels};
    DrawArgs args(context);
    args.dstColorSpace = options.renderColorSpace;
    args.rasterization = &state;
    // Culling an intermediate subtree against only the final output region would discard the
    // content a parent filter reads outside that region. Offscreen clips bound the actual work.
    args.renderRects = nullptr;
    BackgroundSnapshotMap snapshots;
    snapshots.rasterizeStyleSources = false;
    if (needsBackground) {
      auto source = BackgroundSource::Make(context, captureRect, 0, 0, rootToOutput,
                                           options.renderColorSpace, true);
      if (!source) {
        error = RasterizeResult::AllocationFailed;
        return false;
      }
      // An empty cull list means full traversal, while the capture surface still clips pixels.
      // Keeping the full root preserves group isolation, ancestor transforms and prior siblings.
      auto* captureCanvas = source->getCanvas();
      BackgroundCapturer capturer(&snapshots, std::move(source));
      args.backgroundHandler = &capturer;
      root->drawLayer(args, captureCanvas, 1.f, BlendMode::SrcOver);
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(options.clearColor);
    auto matrix = options.matrix;
    matrix.postTranslate(-drawRect.left, -drawRect.top);
    canvas->setMatrix(matrix);
    BackgroundConsumer consumer(&snapshots, false);
    args.backgroundHandler =
        needsBackground ? static_cast<BackgroundHandler*>(&consumer) : BackgroundHandler::NoOp();
    target->drawLayer(args, canvas, target->alpha(), BlendMode::SrcOver);
    if (state.failed) {
      error = RasterizeResult::RenderFailed;
      return false;
    }
    auto info = ImageInfo::Make(static_cast<int>(core.width()), static_cast<int>(core.height()),
                                ColorType::RGBA_8888, AlphaType::Premultiplied, rowBytes,
                                options.outputColorSpace);
    if (info.isEmpty() ||
        !surface->readPixels(info, pixels, static_cast<int>(core.left - drawRect.left),
                             static_cast<int>(core.top - drawRect.top))) {
      error = RasterizeResult::RenderFailed;
      return false;
    }
    return true;
  }
};

RasterizeResult LayerRasterizer::ReadRows(Context* context, const std::shared_ptr<Layer>& target,
                                          const LayerRasterizeOptions& options,
                                          const std::function<bool(int, const Pixmap&)>& onRows) {
  constexpr int MaxExactDimension = 1 << 20;
  if (!context || !target || !target->_root || !onRows || options.size.width <= 0 ||
      options.size.height <= 0 || options.size.width > MaxExactDimension ||
      options.size.height > MaxExactDimension || !options.matrix.isFinite() ||
      options.firstRow < 0 || options.firstRow >= options.size.height || options.rowCount < 0 ||
      options.rowCount > options.size.height - options.firstRow || options.maxTileSize < 0 ||
      (options.maxTileSize > 0 && options.maxTileSize <= 2) || options.maxSurfacePixels == 0) {
    return RasterizeResult::InvalidInput;
  }
  const bool needsBackground = target->hasBackgroundStyle();
  if (options.matrix.hasPerspective() ||
      !Impl::SupportsTree(needsBackground ? target->_root : target.get())) {
    return RasterizeResult::UnsupportedTransform;
  }
  auto* root = target->_root;
  root->updateRenderBounds();
  Matrix inverse;
  auto global = target->getGlobalMatrix().asMatrix();
  if (global.hasPerspective() || !global.invert(&inverse))
    return RasterizeResult::UnsupportedTransform;
  auto rootToOutput = options.matrix;
  rootToOutput.preConcat(inverse);
  const int deviceLimit = static_cast<int>(context->gpu()->limits()->maxTextureDimension2D);
  const int limit =
      options.maxTileSize > 0 ? std::min(options.maxTileSize, deviceLimit) : deviceLimit;
  const size_t rowBytes = static_cast<size_t>(options.size.width) * 4U;
  if (limit <= 2 || rowBytes > options.maxRowBufferBytes) return RasterizeResult::InvalidInput;
  const int endRow =
      options.rowCount == 0 ? options.size.height : options.firstRow + options.rowCount;
  // Both dimensions are positive here. Keep allocation arithmetic unsigned; the row count is
  // bounded by size.height (at most MaxExactDimension), so the later int conversion is safe.
  const auto maxRowCount =
      std::min(static_cast<size_t>(std::min(endRow - options.firstRow, limit - 2)),
               options.maxRowBufferBytes / rowBytes);
  const auto maxRows = static_cast<int>(maxRowCount);
  std::unique_ptr<uint8_t[]> pixels(new (std::nothrow) uint8_t[rowBytes * maxRowCount]);
  if (!pixels) return RasterizeResult::AllocationFailed;
  Impl impl{context, target.get(), root, options, rootToOutput, limit, needsBackground};
  struct Tile {
    Rect core;
    Rect draw;
    Rect capture;
  };
  std::vector<Tile> tiles;
  for (int y = options.firstRow; y < endRow;) {
    int height = std::min(maxRows, endRow - y);
    bool planned = false;
    while (!planned) {
      tiles.clear();
      planned = true;
      for (int x = 0; x < options.size.width;) {
        int width = std::min(limit - 2, options.size.width - x);
        Tile tile;
        for (;;) {
          tile.core = Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                     static_cast<float>(width), static_cast<float>(height));
          if (impl.plan(tile.core, &tile.draw, &tile.capture)) break;
          if (impl.error != RasterizeResult::Success) return impl.error;
          if (width == 1) {
            planned = false;
            break;
          }
          width = std::max(1, width / 2);
        }
        if (!planned) break;
        tiles.push_back(tile);
        x += width;
      }
      if (!planned) {
        if (height == 1) return RasterizeResult::DependencyTooLarge;
        height = std::max(1, height / 2);
      }
    }
    for (const auto& tile : tiles) {
      if (!impl.draw(tile.core, tile.draw, tile.capture,
                     pixels.get() + static_cast<size_t>(tile.core.left) * 4U, rowBytes)) {
        return impl.error;
      }
    }
    auto info = ImageInfo::Make(options.size.width, height, ColorType::RGBA_8888,
                                AlphaType::Premultiplied, rowBytes, options.outputColorSpace);
    if (!onRows(y, Pixmap(info, pixels.get()))) return RasterizeResult::Cancelled;
    y += height;
  }
  return RasterizeResult::Success;
}
}  // namespace tgfx
