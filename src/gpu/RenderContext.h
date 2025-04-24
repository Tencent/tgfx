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
  RenderContext(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags,
                bool clearAll = false, Surface* surface = nullptr);

  Context* getContext() const {
    return renderTarget->getContext();
  }

  void drawFill(const MCState& state, const Fill& fill) override;

  void drawRect(const Rect& rect, const MCState& state, const Fill& fill) override;

  void drawRRect(const RRect& rRect, const MCState& state, const Fill& fill) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const Fill& fill) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                     const SamplingOptions& sampling, const MCState& state,
                     const Fill& fill) override;

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                        const Fill& fill, const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const Fill& fill) override;
  void drawAtlas(const MCState& mcState, std::shared_ptr<Image> atlas, const Matrix matrix[],
                 const Rect tex[], const Color colors[], size_t count,
                 const SamplingOptions& sampling, const Fill& fill);

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
                       const Fill& fill);
  OpsCompositor* getOpsCompositor(bool discardContent = false);
  void replaceRenderTarget(std::shared_ptr<RenderTargetProxy> newRenderTarget,
                           std::shared_ptr<Image> oldContent);
  void glyphDirectMaskDrawing(const GlyphRun& glyphRun, const MCState& state, const Fill& fill,
                              const Stroke* stroke, GlyphRun& rejectedGlyphRun);
  void glyphPathDrawing(const GlyphRun& glyphRun, const MCState& state, const Fill& fill,
                        const Stroke* stroke, GlyphRun& rejectedGlyphRun);

  friend class Surface;
};
}  // namespace tgfx
