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

#include <optional>
#include "core/DrawContext.h"
#include "gpu/OpContext.h"
#include "gpu/ops/DrawOp.h"

namespace tgfx {
class RenderContext : public DrawContext {
 public:
  RenderContext(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags);

  Context* getContext() const {
    return opContext.renderTarget->getContext();
  }

  std::shared_ptr<RenderTargetProxy> renderTarget() const {
    return opContext.renderTarget;
  }

  uint32_t renderFlags() const {
    return opContext.renderFlags;
  }

  uint32_t contentVersion() const {
    return _contentVersion;
  }

  void drawStyle(const MCState& state, const FillStyle& style) override;

  void drawRect(const Rect& rect, const MCState& state, const FillStyle& style) override;

  void drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                 const FillStyle& style) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const FillStyle& style) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                     const SamplingOptions& sampling, const MCState& state,
                     const FillStyle& style) override;

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const Stroke* stroke,
                        const MCState& state, const FillStyle& style) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const FillStyle& style) override;

  std::shared_ptr<Image> makeImageSnapshot();

 private:
  OpContext opContext;
  uint32_t _contentVersion = 1u;
  UniqueKey clipKey = {};
  std::shared_ptr<TextureProxy> clipTexture = nullptr;
  std::shared_ptr<Image> cachedImage = nullptr;

  std::shared_ptr<TextureProxy> getClipTexture(const Path& clip, AAType aaType);
  std::pair<std::optional<Rect>, bool> getClipRect(const Path& clip);
  std::unique_ptr<FragmentProcessor> getClipMask(const Path& clip, const Rect& deviceBounds,
                                                 AAType aaType, Rect* scissorRect);
  Rect getClipBounds(const Path& clip);
  void drawColorGlyphs(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                       const FillStyle& style);
  void addDrawOp(std::unique_ptr<DrawOp> op, const Rect& localBounds, const MCState& state,
                 const FillStyle& style);
  void addOp(std::unique_ptr<Op> op, const std::function<bool()>& willDiscardContent);
  AAType getAAType(const FillStyle& style) const;
  bool aboutToDraw(const std::function<bool()>& willDiscardContent);
  bool wouldOverwriteEntireRT(const Rect& localBounds, const MCState& state, const FillStyle& style,
                              bool isRectOp) const;

  friend class Surface;
};
}  // namespace tgfx
