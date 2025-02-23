/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") {} you may not use this file except
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

#include "core/RecordingContext.h"
#include "utils/Log.h"

namespace tgfx {
/**
 * This const is used to strike a balance between the speed of referencing a sub-picture into a
 * parent picture and the playback cost of recursing into the sub-picture to access its actual
 * operations. Currently, it is set to a conservatively small value. However, based on measurements
 * and other factors such as the type of operations contained, this value may need to be adjusted.
 */
constexpr int MaxPictureDrawsToUnrollInsteadOfReference = 1;

RecordingContext::RecordingContext() {
  memoryCache = std::make_shared<MemoryCache>();
}

std::shared_ptr<Picture> RecordingContext::finishRecordingAsPicture() {
  if (records.empty()) {
    return nullptr;
  }
  bool hasUnboundedFill = false;
  for (auto& record : records) {
    if (!record->state.clip.isInverseFillType()) {
      continue;
    }
    switch (record->type()) {
      case RecordType::DrawStyle:
        hasUnboundedFill = true;
        break;
      case RecordType::DrawShape:
        if (static_cast<DrawShape*>(record)->shape->isInverseFillType()) {
          hasUnboundedFill = true;
        }
        break;
      case RecordType::DrawPicture:
        if (static_cast<DrawPicture*>(record)->picture->hasUnboundedFill()) {
          hasUnboundedFill = true;
        }
        break;
      case RecordType::DrawLayer:
        if (static_cast<DrawLayer*>(record)->picture->hasUnboundedFill()) {
          hasUnboundedFill = true;
        }
        break;
      default:
        break;
    }
    if (hasUnboundedFill) {
      break;
    }
  }
  return std::shared_ptr<Picture>(new Picture(memoryCache, std::move(records), hasUnboundedFill));
}

void RecordingContext::clear() {
  for (auto& record : records) {
    record->~Record();
    memoryCache->release(record);
    records.clear();
  }
  memoryCache->resetCache();
}

void RecordingContext::drawStyle(const MCState& state, const FillStyle& style) {
  if (state.clip.isInverseFillType() && state.clip.isEmpty() && style.isOpaque()) {
    // The clip is wide open, and the style is opaque, so we can discard all previous records as
    // they are now invisible.
    clear();
  }
  if (style.color.alpha > 0.0f) {
    void *buffer = memoryCache->allocate(sizeof(DrawStyle));
    records.push_back(new (buffer) DrawStyle(state, style));
  }
}

void RecordingContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  void* buffer = memoryCache->allocate(sizeof(DrawRect));
  records.push_back(new (buffer) DrawRect(rect, state, style));
}

void RecordingContext::drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) {
  void* buffer = memoryCache->allocate(sizeof(DrawRRect));
  records.push_back(new (buffer) DrawRRect(rRect, state, style));
}

void RecordingContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                 const FillStyle& style) {
  DEBUG_ASSERT(shape != nullptr);
  void *buffer = memoryCache->allocate(sizeof(DrawShape));
  records.push_back(new (buffer) DrawShape(std::move(shape), state, style));
}

void RecordingContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                 const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(image != nullptr);
  void *buffer = memoryCache->allocate(sizeof(DrawImage));
  records.push_back(new (buffer) DrawImage(std::move(image), sampling, state, style));
}

void RecordingContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                     const SamplingOptions& sampling, const MCState& state,
                                     const FillStyle& style) {
  DEBUG_ASSERT(image != nullptr);
  void *buffer = memoryCache->allocate(sizeof(DrawImageRect));
  records.push_back(new (buffer) DrawImageRect(std::move(image), rect, sampling, state, style));
}

void RecordingContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                        const Stroke* stroke, const MCState& state,
                                        const FillStyle& style) {
  if (stroke) {
    void *buffer = memoryCache->allocate(sizeof(StrokeGlyphRunList));
    records.push_back(new (buffer) StrokeGlyphRunList(std::move(glyphRunList), *stroke, state, style));
  } else {
    void *buffer = memoryCache->allocate(sizeof(DrawGlyphRunList));
    records.push_back(new (buffer) DrawGlyphRunList(std::move(glyphRunList), state, style));
  }
}

void RecordingContext::drawLayer(std::shared_ptr<Picture> picture,
                                 std::shared_ptr<ImageFilter> filter, const MCState& state,
                                 const FillStyle& style) {
  DEBUG_ASSERT(picture != nullptr);
  void *buffer = memoryCache->allocate(sizeof(DrawLayer));
  records.push_back(new (buffer) DrawLayer(std::move(picture), std::move(filter), state, style));
}

void RecordingContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  if (picture->records.size() > MaxPictureDrawsToUnrollInsteadOfReference) {
    void *buffer = memoryCache->allocate(sizeof(DrawPicture));
    records.push_back(new (buffer) DrawPicture(picture, state));
  } else {
    picture->playback(this, state);
  }
}
}  // namespace tgfx
