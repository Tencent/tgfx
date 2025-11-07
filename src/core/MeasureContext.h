/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "core/DrawContext.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class MeasureContext : public DrawContext {
 public:
  explicit MeasureContext(bool computeTightBounds = false)
      : computeTightBounds(computeTightBounds) {
  }

  Rect getBounds() const {
    return bounds;
  }

  void drawFill(const Fill& fill) override;

  void drawRect(const Rect& rect, const MCState& state, const Fill& fill,
                const Stroke* stroke) override;

  void drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                 const Stroke* stroke) override;

  void drawPath(const Path& path, const MCState& state, const Fill& fill) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill,
                 const Stroke* stroke) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const Fill& fill) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const MCState& state, const Fill& fill,
                     SrcRectConstraint constraint) override;

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                        const Fill& fill, const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const Fill& fill) override;

 private:
  bool computeTightBounds = false;
  Rect bounds = {};

  void addTightBounds(const Path& path, const MCState& state);

  void addLocalBounds(const MCState& state, const Rect& localBounds, bool unbounded = false);
  void addDeviceBounds(const Path& clip, const Rect& deviceBounds, bool unbounded = false);
};
}  // namespace tgfx
