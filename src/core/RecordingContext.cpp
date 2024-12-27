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
#include "gpu/Blend.h"

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
  records.resize(0);
}

void RecordingContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  records.push_back(new DrawRect(rect, state, style));
}

void RecordingContext::drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) {
  records.push_back(new DrawRRect(rRect, state, style));
}

void RecordingContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                 const FillStyle& style) {
  if (shape == nullptr) {
    return;
  }
  records.push_back(new DrawShape(std::move(shape), state, style));
}

void RecordingContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                 const MCState& state, const FillStyle& style) {
  if (image == nullptr) {
    return;
  }
  records.push_back(new DrawImage(std::move(image), sampling, state, style));
}

void RecordingContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                     const SamplingOptions& sampling, const MCState& state,
                                     const FillStyle& style) {
  if (image == nullptr) {
    return;
  }
  records.push_back(new DrawImageRect(std::move(image), rect, sampling, state, style));
}

void RecordingContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                        const Stroke* stroke, const MCState& state,
                                        const FillStyle& style) {
  if (stroke) {
    records.push_back(new StrokeGlyphRunList(std::move(glyphRunList), *stroke, state, style));
  } else {
    records.push_back(new DrawGlyphRunList(std::move(glyphRunList), state, style));
  }
}

void RecordingContext::drawLayer(std::shared_ptr<Picture> picture,
                                 std::shared_ptr<ImageFilter> filter, const MCState& state,
                                 const FillStyle& style) {
  if (picture == nullptr) {
    return;
  }
  records.push_back(new DrawLayer(std::move(picture), std::move(filter), state, style));
}

void RecordingContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  if (picture == nullptr) {
    return;
  }
  if (picture->records.size() > MaxPictureDrawsToUnrollInsteadOfReference) {
    records.push_back(new DrawPicture(picture, state));
  } else {
    picture->playback(this, state);
  }
}
}  // namespace tgfx
