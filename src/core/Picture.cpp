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

#include "tgfx/core/Picture.h"
#include "core/MeasureContext.h"
#include "core/PictureRecords.h"
#include "core/shaders/ImageShader.h"
#include "core/utils/AtomicCache.h"
#include "core/utils/BlockAllocator.h"
#include "core/utils/Log.h"
#include "core/utils/Types.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "utils/MathExtra.h"

namespace tgfx {
Picture::Picture(std::unique_ptr<BlockBuffer> buffer,
                 std::vector<PlacementPtr<PictureRecord>> recordList, size_t drawCount)
    : blockBuffer(std::move(buffer)), records(std::move(recordList)), drawCount(drawCount) {
  DEBUG_ASSERT(blockBuffer != nullptr);
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
  AtomicCacheReset(bounds);
}

Rect Picture::getBounds() const {
  if (auto cachedBounds = AtomicCacheGet(bounds)) {
    return *cachedBounds;
  }
  MeasureContext context;
  MCState state(Matrix::I());
  playback(&context, state);
  auto totalBounds = context.getBounds();
  AtomicCacheSet(bounds, &totalBounds);
  return totalBounds;
}

void Picture::playback(Canvas* canvas, AbortCallback* callback) const {
  if (canvas == nullptr) {
    return;
  }
  playback(canvas->drawContext, *canvas->mcState, callback);
}

void Picture::playback(DrawContext* drawContext, const MCState& state,
                       AbortCallback* callback) const {
  DEBUG_ASSERT(drawContext != nullptr);
  PlaybackContext playbackContext(state);
  for (auto& record : records) {
    if (callback && callback->abort()) {
      break;
    }
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
  Brush brush = {};
  bool hasStroke = false;
  auto record = getFirstDrawRecord(&state, &brush, &hasStroke);
  if (record == nullptr || hasStroke) {
    return nullptr;
  }

  if (brush.maskFilter || brush.colorFilter || brush.color.alpha != 1.0f) {
    return nullptr;
  }

  std::shared_ptr<Image> image = nullptr;
  SamplingOptions sampling = {};

  // Check for direct DrawImage or DrawImageRect
  if (record->type() == PictureRecordType::DrawImage ||
      record->type() == PictureRecordType::DrawImageRect) {
    auto imageRecord = static_cast<const DrawImage*>(record);
    image = imageRecord->image;
    sampling = imageRecord->sampling;
    if (image->isAlphaOnly()) {
      if (brush.shader || brush.color != Color::White()) {
        return nullptr;
      }
    }
    if (record->type() == PictureRecordType::DrawImageRect) {
      image = image->makeSubset(static_cast<const DrawImageRect*>(record)->rect);
      DEBUG_ASSERT(image != nullptr);
    }
  }
  // Check for DrawRect with ImageShader (rect + ImageShader pattern)
  else if (record->type() == PictureRecordType::DrawRect) {
    if (!brush.shader || !brush.shader->isAImage()) {
      return nullptr;
    }
    auto shaderType = Types::Get(brush.shader.get());
    if (shaderType != Types::ShaderType::Image) {
      return nullptr;
    }
    auto imageShader = static_cast<const ImageShader*>(brush.shader.get());
    if (!imageShader->image || imageShader->tileModeX != TileMode::Clamp ||
        imageShader->tileModeY != TileMode::Clamp) {
      return nullptr;
    }
    auto rect = static_cast<const DrawRect*>(record)->rect;
    // Check if rect matches image size and is at origin
    auto imageWidth = static_cast<float>(imageShader->image->width());
    auto imageHeight = static_cast<float>(imageShader->image->height());
    if (rect.left != 0 || rect.top != 0 || rect.width() != imageWidth ||
        rect.height() != imageHeight) {
      return nullptr;
    }
    image = imageShader->image;
    sampling = imageShader->sampling;
  } else {
    return nullptr;
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

const PictureRecord* Picture::getFirstDrawRecord(MCState* state, Brush* brush,
                                                 bool* hasStroke) const {
  PlaybackContext playback = {};
  PictureRecord* drawRecord = nullptr;
  for (auto& record : records) {
    if (record->type() >= PictureRecordType::DrawFill) {
      drawRecord = record.get();
      break;
    }
    record->playback(nullptr, &playback);
  }
  if (state) {
    *state = playback.state();
  }
  if (brush) {
    *brush = playback.brush();
  }
  if (hasStroke) {
    *hasStroke = playback.stroke() != nullptr;
  }
  return drawRecord;
}

}  // namespace tgfx
