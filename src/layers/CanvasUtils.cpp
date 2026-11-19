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

#include "layers/CanvasUtils.h"
#include "core/filters/ShaderMaskFilter.h"
#include "layers/DrawArgs.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

std::optional<Rect> GetClipBounds(const Canvas* canvas) {
  if (canvas == nullptr) {
    return std::nullopt;
  }
  const auto clipBounds = canvas->getTotalClipBounds();
  auto clipRect = Rect::MakeEmpty();
  auto surface = canvas->getSurface();
  if (!clipBounds.has_value()) {
    if (!surface) {
      return std::nullopt;
    }
    clipRect = Rect::MakeWH(surface->width(), surface->height());
  } else {
    clipRect = *clipBounds;
    if (surface && !clipRect.intersect(Rect::MakeWH(surface->width(), surface->height()))) {
      return Rect::MakeEmpty();
    }
  }
  if (clipRect.isEmpty()) {
    return Rect::MakeEmpty();
  }
  auto inverse = Matrix::I();
  if (!canvas->getMatrix().invert(&inverse)) {
    return Rect::MakeEmpty();
  }
  clipRect = inverse.mapRect(clipRect);
  clipRect.roundOut();
  return clipRect;
}

static bool HasEmptyClip(const Canvas* canvas) {
  auto bounds = canvas->getTotalClipBounds();
  return bounds.has_value() && bounds->isEmpty();
}

bool LayerClip::ApplyAncestorClips(const std::vector<Layer*>& clippedAncestors, Layer* target,
                                   const DrawArgs& args, Canvas* canvas,
                                   std::shared_ptr<Shader>* coverage) {
  const auto targetToTile = canvas->getMatrix();
  auto maskArgs = args;
  maskArgs.renderRects = nullptr;
  for (auto it = clippedAncestors.rbegin(); it != clippedAncestors.rend(); ++it) {
    auto* ancestor = *it;
    auto ancestorToTile = targetToTile;
    ancestorToTile.preConcat(ancestor->getRelativeMatrix(target));
    canvas->setMatrix(ancestorToTile);
    if (ancestor->_scrollRect != nullptr) {
      canvas->clipRect(*ancestor->_scrollRect, ancestor->bitFields.allowsEdgeAntialiasing);
    }
    std::shared_ptr<MaskFilter> maskFilter;
    if (HasEmptyClip(canvas) || !ancestor->prepareMask(maskArgs, canvas, &maskFilter)) {
      return false;
    }
    if (coverage != nullptr && maskFilter != nullptr) {
      // Layer::prepareMask produces a shader mask in the ancestor's local space.
      auto* mask = static_cast<const ShaderMaskFilter*>(maskFilter.get());
      auto shader = mask->getShader()->makeWithMatrix(ancestorToTile);
      if (mask->isInverted()) {
        shader = Shader::MakeBlend(BlendMode::DstOut, Shader::MakeColorShader(Color::White()),
                                   std::move(shader));
      }
      *coverage = *coverage != nullptr
                      ? Shader::MakeBlend(BlendMode::SrcIn, std::move(*coverage), std::move(shader))
                      : std::move(shader);
    }
  }
  canvas->setMatrix(targetToTile);
  return !HasEmptyClip(canvas);
}

}  // namespace tgfx
