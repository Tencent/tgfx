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

#include "tgfx/core/Mask.h"
#include "core/GlyphRun.h"
#include "core/ImageStream.h"
#include "tgfx/core/PathEffect.h"

namespace tgfx {
void Mask::fillPath(const Path& path, const Stroke* stroke) {
  if (path.isEmpty()) {
    return;
  }
  auto effect = PathEffect::MakeStroke(stroke);
  if (effect != nullptr) {
    auto newPath = path;
    effect->applyTo(&newPath);
    onFillPath(newPath, matrix);
  } else {
    onFillPath(path, matrix);
  }
}

bool Mask::fillText(const TextBlob* textBlob, const Stroke* stroke) {
  if (textBlob == nullptr) {
    return false;
  }
  auto runCount = textBlob->glyphRunCount();
  float maxScale = -1.0f;
  auto pathMatrix = matrix;
  for (size_t i = 0; i < runCount; ++i) {
    auto glyphRun = textBlob->getGlyphRun(i);
    if (glyphRun->hasColor()) {
      return false;
    }
    if (onFillText(glyphRun, stroke, matrix)) {
      continue;
    }
    if (maxScale == -1.0f) {
      maxScale = matrix.getMaxScale();
      if (maxScale <= 0) {
        return false;
      }
      pathMatrix.preScale(1.0f / maxScale, 1.0f / maxScale);
    }
    Path path = {};
    if (!glyphRun->getPath(&path, maxScale, stroke)) {
      return false;
    }
    onFillPath(path, pathMatrix, true);
  }
  return true;
}

bool Mask::onFillText(const GlyphRun*, const Stroke*, const Matrix&) {
  return false;
}
}  // namespace tgfx
