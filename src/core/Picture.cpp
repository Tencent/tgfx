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
#include "core/utils/BlockBuffer.h"
#include "core/utils/Log.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"

namespace tgfx {
Picture::Picture(std::shared_ptr<BlockData> data, std::vector<PlacementPtr<Record>> recordList,
                 size_t drawCount)
    : blockData(std::move(data)), records(std::move(recordList)), drawCount(drawCount) {
  DEBUG_ASSERT(blockData != nullptr);
  DEBUG_ASSERT(!records.empty());
  bool hasInverseClip = true;
  for (auto& record : records) {
    if (record->hasUnboundedFill(hasInverseClip)) {
      _hasUnboundedFill = true;
      break;
    }
  }
}

Picture::~Picture() {
  // Make sure the records are cleared before the block data is destroyed.
  records.clear();
}

Rect Picture::getBounds(const Matrix* matrix) const {
  MeasureContext context = {};
  MCState state(matrix ? *matrix : Matrix::I());
  playback(&context, state);
  return context.getBounds();
}

void Picture::playback(Canvas* canvas) const {
  if (canvas != nullptr) {
    playback(canvas->drawContext, *canvas->mcState);
  }
}

void Picture::playback(DrawContext* drawContext, const MCState& state) const {
  DEBUG_ASSERT(drawContext != nullptr);
  if (state.clip.isEmpty() && !state.clip.isInverseFillType()) {
    return;
  }
  TransformContext transformContext(drawContext, state);
  if (transformContext.type() != TransformContext::Type::None) {
    drawContext = &transformContext;
  }
  PlaybackContext playbackContext = {};
  for (auto& record : records) {
    record->playback(drawContext, &playbackContext);
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
  if (drawCount != 1) {
    return nullptr;
  }
  MCState state = {};
  Fill fill = {};
  auto record = firstDrawRecord(&state, &fill);
  if (record == nullptr) {
    return nullptr;
  }
  if (record->type() != RecordType::DrawImage && record->type() != RecordType::DrawImageRect) {
    return nullptr;
  }
  auto imageRecord = static_cast<const DrawImage*>(record);
  if (fill.maskFilter || fill.colorFilter) {
    return nullptr;
  }
  auto image = imageRecord->image;
  if (image->isAlphaOnly()) {
    if (fill.shader || fill.color != Color::White()) {
      return nullptr;
    }
  }
  auto imageMatrix = state.matrix;
  if (matrix) {
    imageMatrix.postConcat(*matrix);
  }
  if (!imageMatrix.isTranslate()) {
    return nullptr;
  }
  Rect clipRect = {};
  if (!GetClipRect(state.clip, matrix, &clipRect)) {
    return nullptr;
  }
  Rect subset = {};
  if (clipSize != nullptr) {
    subset = Rect::MakeWH(clipSize->width, clipSize->height);
    if (!clipRect.isEmpty() && !clipRect.contains(subset)) {
      return nullptr;
    }
  } else if (!clipRect.isEmpty()) {
    subset = clipRect;
  }
  if (record->type() == RecordType::DrawImageRect) {
    image = image->makeSubset(static_cast<const DrawImageRect*>(record)->rect);
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

const Record* Picture::firstDrawRecord(tgfx::MCState* state, tgfx::Fill* fill) const {
  for (auto& record : records) {
    if (record->type() > RecordType::SetFill) {
      return record.get();
    }
    switch (record->type()) {
      case RecordType::SetMatrix:
        if (state) {
          state->matrix = static_cast<SetMatrix*>(record.get())->matrix;
        }
        break;
      case RecordType::SetClip:
        if (state) {
          state->clip = static_cast<SetClip*>(record.get())->clip;
        }
        break;
      case RecordType::SetColor:
        if (fill) {
          fill->color = static_cast<SetColor*>(record.get())->color;
        }
        break;
      case RecordType::SetFill:
        if (fill) {
          *fill = static_cast<SetFill*>(record.get())->fill;
        }
        break;
      default:
        break;
    }
  }
  return nullptr;
}

}  // namespace tgfx