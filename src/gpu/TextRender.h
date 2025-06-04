/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/GlyphRunList.h"
#include "core/MCState.h"
#include "core/atlas/AtlasManager.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class TextRender {
 public:
  static std::unique_ptr<TextRender> MakeFrom(Context* context, OpsCompositor* opsCompositor,
                                              std::shared_ptr<GlyphRunList> glyphRunList,
                                              const Rect& clipBounds);

  void draw(const MCState& state, const Fill& fill, const Stroke* stroke) const;

 private:
  TextRender(Context* context, OpsCompositor* opsCompositor,
             std::shared_ptr<GlyphRunList> glyphRunList, const Rect& clipBounds);
  void directMaskDrawing(const GlyphRun& glyphRun, const MCState& state, const Fill& fill,
                         const Stroke* stroke, GlyphRun& rejectedGlyphRun) const;
  void pathDrawing(const GlyphRun& glyphRun, const MCState& state, const Fill& fill,
                   const Stroke* stroke, GlyphRun& rejectedGlyphRun) const;

  void transformedMaskDrawing(const GlyphRun& glyphRun, const MCState& state, const Fill& fill,
                              const Stroke* stroke) const;
  void drawGlyphAtlas(std::shared_ptr<TextureProxy> textureProxy, const Rect& rect,
                      const SamplingOptions& sampling, const MCState& state, const Fill& fill,
                      const Matrix& viewMatrix) const;

  Context* context = nullptr;
  OpsCompositor* opsCompositor = nullptr;
  std::shared_ptr<GlyphRunList> glyphRunList = nullptr;
  AtlasManager* atlasManager = nullptr;
  Rect clipBounds = {};
};
}  // namespace tgfx
