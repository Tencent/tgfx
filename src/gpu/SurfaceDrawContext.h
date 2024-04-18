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

namespace tgfx {
class SurfaceDrawContext : public DrawContext {
 public:
  SurfaceDrawContext(std::shared_ptr<RenderTargetProxy> renderTargetProxy, uint32_t renderFlags);

  ~SurfaceDrawContext() override;

  Surface* getSurface() const override {
    return surface;
  }

  void clear() override;

  void drawRect(const Rect& rect, const FillStyle& style) override;

  void drawRRect(const RRect& rRect, const FillStyle& style) override;

  void drawPath(const Path& path, const FillStyle& style, const Stroke* stroke) override;

  void drawImageRect(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                     const Rect& rect, const FillStyle& style) override;

  void drawGlyphRun(GlyphRun glyphRun, const FillStyle& style, const Stroke* stroke) override;

 private:
  OpContext* opContext = nullptr;
  uint32_t renderFlags = 0;
  Surface* surface = nullptr;
  std::shared_ptr<TextureProxy> clipTexture = nullptr;
  uint32_t clipID = 0;

  explicit SurfaceDrawContext(Surface* surface);
  Context* getContext() const;
  std::shared_ptr<TextureProxy> getClipTexture();
  std::pair<std::optional<Rect>, bool> getClipRect(const Rect* drawBounds = nullptr);
  std::unique_ptr<FragmentProcessor> getClipMask(const Rect& deviceBounds, const Matrix& viewMatrix,
                                                 Rect* scissorRect);
  FPArgs makeFPArgs(const Rect& localBounds, const Matrix& viewMatrix);
  std::unique_ptr<FragmentProcessor> makeTextureMask(const Path& path, const Matrix& viewMatrix,
                                                     const Stroke* stroke = nullptr);
  bool drawAsClear(const Rect& rect, const Matrix& viewMatrix, const FillStyle& style);
  void drawImageRect(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                     const Rect& rect, const Matrix& viewMatrix, const FillStyle& style);
  void drawColorGlyphs(const GlyphRun& glyphRun, const FillStyle& style);
  void addDrawOp(std::unique_ptr<DrawOp> op, const FPArgs& args, const FillStyle& style,
                 bool ignoreShader = false);
  void addOp(std::unique_ptr<Op> op, const std::function<bool()>& willDiscardContent);
  void replaceRenderTarget(std::shared_ptr<RenderTargetProxy> newRenderTargetProxy);
  bool wouldOverwriteEntireRT(const FPArgs& args, const FillStyle& style, bool isRectOp) const;

  friend class Surface;
};
}  // namespace tgfx
