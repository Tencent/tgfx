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

#include "tgfx/core/Picture.h"
#include "core/MeasureContext.h"
#include "core/Records.h"
#include "core/TransformContext.h"
#include "core/utils/Log.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"

namespace tgfx {
Picture::Picture(std::vector<Record*> records) : records(std::move(records)) {
}

Picture::~Picture() {
  for (auto& record : records) {
    delete record;
  }
}

Rect Picture::getBounds(const Matrix& matrix) const {
  MeasureContext context = {};
  MCState state(matrix);
  playback(&context, state);
  return context.getBounds();
}

void Picture::playback(Canvas* canvas) const {
  if (canvas == nullptr) {
    return;
  }
  playback(canvas->drawContext, *canvas->mcState);
}

void Picture::playback(DrawContext* drawContext, const MCState& state) const {
  DEBUG_ASSERT(drawContext != nullptr);
  std::unique_ptr<TransformContext> transformContext;
  auto surface = drawContext->getSurface();
  Rect rect = {};
  if (surface && state.clip.isRect(&rect) &&
      rect == Rect::MakeWH(surface->width(), surface->height())) {
    transformContext = TransformContext::Make(drawContext, state.matrix);
  } else {
    transformContext = TransformContext::Make(drawContext, state.matrix, state.clip);
  }
  if (transformContext) {
    drawContext = transformContext.get();
  } else if (state.clip.isEmpty() && !state.clip.isInverseFillType()) {
    return;
  }
  for (auto& record : records) {
    record->playback(drawContext);
  }
}

std::shared_ptr<Image> Picture::asImage(int width, int height, const Matrix* matrix) const {
  if (records.size() != 1) {
    return nullptr;
  }
  auto record = records[0];
  if (record->type() != RecordType::DrawImage) {
    return nullptr;
  }
  auto imageRecord = static_cast<DrawImage*>(record);
  auto image = imageRecord->image;
  if (image->isAlphaOnly()) {
    return nullptr;
  }
  auto& style = imageRecord->style;
  if (style.colorFilter || style.maskFilter) {
    return nullptr;
  }
  auto imageMatrix = imageRecord->state.matrix;
  if (matrix) {
    imageMatrix.postConcat(*matrix);
  }
  if (!imageMatrix.isTranslate()) {
    return nullptr;
  }
  auto offsetX = imageMatrix.getTranslateX();
  auto offsetY = imageMatrix.getTranslateY();
  if (roundf(offsetX) != offsetX || roundf(offsetY) != offsetY) {
    return nullptr;
  }
  auto subset =
      Rect::MakeXYWH(-offsetX, -offsetY, static_cast<float>(width), static_cast<float>(height));
  auto clip = imageRecord->state.clip;
  if (clip.isEmpty() && clip.isInverseFillType()) {
    return image->makeSubset(subset);
  }
  Rect clipRect = {};
  if (!clip.isRect(&clipRect)) {
    return nullptr;
  }
  if (matrix) {
    if (!matrix->rectStaysRect()) {
      return nullptr;
    }
    matrix->mapRect(&clipRect);
  }
  if (clipRect != Rect::MakeWH(width, height)) {
    return nullptr;
  }
  return image->makeSubset(subset);
}
}  // namespace tgfx