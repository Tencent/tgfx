/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

namespace tgfx {
class LayerUnrollContext : public DrawContext {
 public:
  explicit LayerUnrollContext(DrawContext* drawContext, FillStyle fillStyle,
                              std::shared_ptr<ImageFilter> filter);

  bool hasUnrolled() const {
    return unrolled;
  }

  void clear() override;

  void drawRect(const Rect& rect, const MCState& state, const FillStyle& style) override;

  void drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) override;

  void drawPath(const Path& path, const MCState& state, const FillStyle& style,
                const Stroke* stroke) override;

  void drawImageRect(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                     const Rect& rect, const MCState& mcState, const FillStyle& style) override;

  void drawGlyphRun(GlyphRun glyphRun, const MCState& state, const FillStyle& style,
                    const Stroke* stroke) override;

  void drawLayer(std::shared_ptr<Picture> picture, const MCState& state, const FillStyle& style,
                 std::shared_ptr<ImageFilter> filter) override;

 protected:
  FillStyle merge(const FillStyle& style);

 private:
  DrawContext* drawContext = nullptr;
  FillStyle fillStyle = {};
  std::shared_ptr<ImageFilter> imageFilter = nullptr;
  bool unrolled = false;
};
}  // namespace tgfx
