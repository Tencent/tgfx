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
      case RecordType::DrawFill:
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
  return std::shared_ptr<Picture>(new Picture(std::move(records), hasUnboundedFill));
}

void RecordingContext::clear() {
  for (auto& record : records) {
    delete record;
  }
  records.clear();
}

void RecordingContext::drawFill(const MCState& state, const Fill& fill) {
  if (state.clip.isInverseFillType() && state.clip.isEmpty() && fill.isOpaque()) {
    // The clip is wide open, and the fill is opaque, so we can discard all previous records as
    // they are now invisible.
    clear();
  }
  if (fill.color.alpha > 0.0f) {
    records.push_back(new DrawFill(state, fill));
  }
}

void RecordingContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  records.push_back(new DrawRect(rect, state, fill));
}

void RecordingContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill) {
  records.push_back(new DrawRRect(rRect, state, fill));
}

void RecordingContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                 const Fill& fill) {
  DEBUG_ASSERT(shape != nullptr);
  records.push_back(new DrawShape(std::move(shape), state, fill));
}

void RecordingContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                 const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(image != nullptr);
  records.push_back(new DrawImage(std::move(image), sampling, state, fill));
}

void RecordingContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                     const SamplingOptions& sampling, const MCState& state,
                                     const Fill& fill) {
  DEBUG_ASSERT(image != nullptr);
  records.push_back(new DrawImageRect(std::move(image), rect, sampling, state, fill));
}

void RecordingContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                        const MCState& state, const Fill& fill,
                                        const Stroke* stroke) {
  if (stroke) {
    records.push_back(new StrokeGlyphRunList(std::move(glyphRunList), state, fill, *stroke));
  } else {
    records.push_back(new DrawGlyphRunList(std::move(glyphRunList), state, fill));
  }
}

void RecordingContext::drawLayer(std::shared_ptr<Picture> picture,
                                 std::shared_ptr<ImageFilter> filter, const MCState& state,
                                 const Fill& fill) {
  DEBUG_ASSERT(picture != nullptr);
  records.push_back(new DrawLayer(std::move(picture), std::move(filter), state, fill));
}

void RecordingContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  if (picture->records.size() > MaxPictureDrawsToUnrollInsteadOfReference) {
    records.push_back(new DrawPicture(picture, state));
  } else {
    picture->playback(this, state);
  }
}
}  // namespace tgfx
