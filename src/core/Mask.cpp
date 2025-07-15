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

#include "tgfx/core/Mask.h"
#include "core/GlyphRunList.h"
#include "core/ImageStream.h"

namespace tgfx {
void Mask::fillPath(const Path& path, const Stroke* stroke) {
  if (path.isEmpty()) {
    if (path.isInverseFillType()) {
      Path tempPath = {};
      tempPath.addRect(Rect::MakeWH(width(), height()));
      onFillPath(tempPath, {});
    }
    return;
  }
  if (stroke != nullptr) {
    auto newPath = path;
    stroke->applyToPath(&newPath);
    onFillPath(newPath, matrix);
  } else {
    onFillPath(path, matrix);
  }
}

bool Mask::fillText(const TextBlob* textBlob, const Stroke* stroke) {
  if (textBlob == nullptr) {
    return false;
  }
  return std::all_of(textBlob->glyphRunLists.begin(), textBlob->glyphRunLists.end(),
                     [this, stroke](const std::shared_ptr<GlyphRunList>& glyphRunList) {
                       return fillText(glyphRunList.get(), stroke);
                     });
}

bool Mask::fillText(const GlyphRunList* glyphRunList, const Stroke* stroke) {
  if (glyphRunList->hasColor()) {
    return false;
  }
  if (onFillText(glyphRunList, stroke, matrix, antiAlias)) {
    return true;
  }
  Path path = {};
  if (!glyphRunList->getPath(&path, &matrix)) {
    return false;
  }
  if (stroke) {
    auto scale = matrix.getMaxScale();
    Stroke scaledStroke(stroke->width * scale, stroke->cap, stroke->join, stroke->miterLimit);
    scaledStroke.applyToPath(&path);
  }
  onFillPath(path, {}, antiAlias, true);
  return true;
}

bool Mask::onFillText(const GlyphRunList*, const Stroke*, const Matrix&, bool) {
  return false;
}
}  // namespace tgfx
