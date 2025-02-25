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
#include "MemoryCache.h"
#include "core/MeasureContext.h"
#include "core/Records.h"
#include "core/TransformContext.h"
#include "core/utils/Log.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"

namespace tgfx {
Picture::Picture(std::shared_ptr<MemoryCache> memoryCache, std::shared_ptr<RecordList> records,
                 bool hasUnboundedFill)
    : memoryCache(std::move(memoryCache)), records(std::move(records)),
      _hasUnboundedFill(hasUnboundedFill) {
  DEBUG_ASSERT(memoryCache == nullptr);
  DEBUG_ASSERT(records == nullptr);
}

Picture::~Picture() {
  auto record = records->front();
  while (record != nullptr) {
    const auto next = record->next;
    record->~Record();
    memoryCache->release(record);
    record = next;
  }
}

Rect Picture::getBounds(const Matrix* matrix) const {
  MeasureContext context = {};
  MCState state(matrix ? *matrix : Matrix::I());
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
  auto transformContext = TransformContext::Make(drawContext, state.matrix, state.clip);
  if (transformContext) {
    drawContext = transformContext.get();
  } else if (state.clip.isEmpty() && !state.clip.isInverseFillType()) {
    return;
  }
  auto record = records->front();
  while (record != nullptr) {
    record->playback(drawContext);
    record = record->next;
  }
}

static bool GetClipRect(const Path& clip, const Matrix* matrix, Rect* clipRect) {
  if (clip.isInverseFillType()) {
    if (clip.isEmpty()) {
      clipRect->setEmpty();
      return true;
    }
    return false;
  }
  Rect rect = {};
  if (!clip.isRect(&rect)) {
    return false;
  }
  if (matrix != nullptr) {
    if (!matrix->rectStaysRect()) {
      return false;
    }
    matrix->mapRect(&rect);
  }
  *clipRect = rect;
  return true;
}

std::shared_ptr<Image> Picture::asImage(Point* offset, const Matrix* matrix,
                                        const ISize* clipSize) const {
  if (records->size() != 1) {
    return nullptr;
  }
  auto record = records->front();
  if (record->type() != RecordType::DrawImage && record->type() != RecordType::DrawImageRect) {
    return nullptr;
  }
  auto imageRecord = static_cast<const DrawImage*>(record);
  auto& style = imageRecord->style;
  if (style.maskFilter || style.colorFilter) {
    return nullptr;
  }
  auto image = imageRecord->image;
  if (image->isAlphaOnly()) {
    if (style.shader || style.color != Color::White()) {
      return nullptr;
    }
  }
  auto imageMatrix = imageRecord->state.matrix;
  if (matrix) {
    imageMatrix.postConcat(*matrix);
  }
  if (!imageMatrix.isTranslate()) {
    return nullptr;
  }
  auto clipRect = Rect::MakeEmpty();
  if (!GetClipRect(imageRecord->state.clip, matrix, &clipRect)) {
    return nullptr;
  }
  auto subset = Rect::MakeEmpty();
  if (clipSize != nullptr) {
    subset = Rect::MakeWH(clipSize->width, clipSize->height);
    if (!clipRect.isEmpty() && !clipRect.contains(subset)) {
      return nullptr;
    }
  } else if (!clipRect.isEmpty()) {
    subset = clipRect;
  }
  if (record->type() == RecordType::DrawImageRect) {
    image = image->makeSubset(static_cast<const DrawImageRect*>(imageRecord)->rect);
    DEBUG_ASSERT(image != nullptr);
  }
  auto offsetX = imageMatrix.getTranslateX();
  auto offsetY = imageMatrix.getTranslateY();
  if (!subset.isEmpty()) {
    subset.offset(-offsetX, -offsetY);
    auto roundSubset = subset;
    roundSubset.round();
    if (roundSubset != subset) {
      return nullptr;
    }
    image = image->makeSubset(subset);
    if (image == nullptr) {
      return nullptr;
    }
    offsetX += subset.left;
    offsetY += subset.top;
  }
  if (offset) {
    offset->set(offsetX, offsetY);
  }
  return image;
}

}  // namespace tgfx
