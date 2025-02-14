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
#include "gpu/OpsCompositor.h"
#include "gpu/ops/DrawOp.h"

namespace tgfx {
class RenderContext : public DrawContext {
 public:
  RenderContext(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags,
                Surface* surface = nullptr);

  Context* getContext() const {
    return renderTarget->getContext();
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

  /**
   * Copies the contents of the render target to the given texture.
   */
  void copyToTexture(std::shared_ptr<TextureProxy> textureProxy, const Rect& srcRect,
                     const Point& dstPoint);

  /**
   * Flushes the render context, submitting all pending operations to the drawing manager. Returns
   * true if any operations were submitted.
   */
  bool flush();

 private:
  std::shared_ptr<RenderTargetProxy> renderTarget = nullptr;
  uint32_t renderFlags = 0;
  Surface* surface = nullptr;
  std::shared_ptr<OpsCompositor> opsCompositor = nullptr;

  Rect getClipBounds(const Path& clip);
  void drawColorGlyphs(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                       const FillStyle& style);
  AAType getAAType(const FillStyle& style) const;
  OpsCompositor* getOpsCompositor(bool discardContent = false);

  friend class Surface;
};
}  // namespace tgfx
