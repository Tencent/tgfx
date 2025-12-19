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

#include <vector>
#include "core/DrawContext.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {

/**
 * MaskContext is a DrawContext implementation that records drawable shapes and generates a combined
 * Path on finish. It also implements Picture::AbortCallback to abort playback when unsupported
 * operations are encountered. It only supports simple filled shapes (Rect, RRect, Path) with opaque
 * brushes. For unsupported operations (Image, GlyphRunList, etc.), it sets an abort flag.
 */
class MaskContext : public DrawContext, public Picture::AbortCallback {
 public:
  /**
   * Extracts a mask path from a Picture. Returns true if successful, false if the picture contains
   * unsupported operations.
   */
  static bool GetMaskPath(const std::shared_ptr<Picture>& picture, Path* result);

  MaskContext() = default;

  /**
   * Finishes recording and generates the combined mask path.
   * Returns true if successful, false if aborted or failed.
   */
  bool finish(Path* result);

  // Picture::AbortCallback implementation
  bool abort() override {
    return _aborted;
  }

  void drawFill(const Brush& brush) override;

  void drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                const Stroke* stroke) override;

  void drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                 const Stroke* stroke) override;

  void drawPath(const Path& path, const MCState& state, const Brush& brush) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Brush& brush,
                 const Stroke* stroke) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const Brush& brush) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const MCState& state, const Brush& brush,
                     SrcRectConstraint constraint) override;

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                        const Brush& brush, const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const Brush& brush) override;

 private:
  struct PathRecord {
    Path path;
    MCState state;
    Stroke stroke = {};
    bool hasStroke = false;
  };

  void addPath(Path path, const MCState& state, const Stroke* stroke);

  std::vector<PathRecord> _records = {};
  bool _aborted = false;
};

}  // namespace tgfx
