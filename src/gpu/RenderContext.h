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

#include <optional>
#include "core/DrawContext.h"
#include "gpu/OpsCompositor.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
class RenderContext : public DrawContext {
 public:
  RenderContext(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags,
                bool clearAll = false, Surface* surface = nullptr, std::shared_ptr<ColorSpace> colorSpace = nullptr);

  Context* getContext() const {
    return renderTarget->getContext();
  }

  void drawFill(const Fill& fill) override;

  void drawRect(const Rect& rect, const MCState& state, const Fill& fill) override;

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

  /**
   * Flushes the render context, submitting all pending operations to the drawing manager. Returns
   * true if any operations were submitted.
   */
  bool flush();

  std::shared_ptr<ColorSpace> colorSpace() const {
    return _colorSpace;
  }

 private:
  void drawGlyphsAsDirectMask(const GlyphRun& sourceGlyphRun, const MCState& state,
                              const Fill& fill, const Stroke* stroke, const Rect& localClipBounds,
                              GlyphRun* rejectedGlyphRun);

  void drawGlyphsAsPath(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                        const Fill& fill, const Stroke* stroke, Rect& localClipBounds);

  void drawGlyphsAsTransformedMask(const GlyphRun& sourceGlyphRun, const MCState& state,
                                   const Fill& fill, const Stroke* stroke);

  std::shared_ptr<RenderTargetProxy> renderTarget = nullptr;
  uint32_t renderFlags = 0;
  Surface* surface = nullptr;
  std::shared_ptr<OpsCompositor> opsCompositor = nullptr;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;

  Rect getClipBounds(const Path& clip);
  OpsCompositor* getOpsCompositor(bool discardContent = false);
  void replaceRenderTarget(std::shared_ptr<RenderTargetProxy> newRenderTarget,
                           std::shared_ptr<Image> oldContent);

  friend class Surface;
};
}  // namespace tgfx
