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

RecordingContext::~RecordingContext() {
  // make sure the records are cleared before the blockBuffer is destroyed.
  records.clear();
}

void RecordingContext::clear() {
  records.clear();
  blockBuffer.clear();
  lastState = {};
  lastFill = {};
  drawCount = 0;
}

std::shared_ptr<Picture> RecordingContext::finishRecordingAsPicture() {
  if (records.empty()) {
    return nullptr;
  }
  auto picture =
      std::shared_ptr<Picture>(new Picture(blockBuffer.release(), std::move(records), drawCount));
  lastState = {};
  lastFill = {};
  drawCount = 0;
  return picture;
}

void RecordingContext::drawFill(const Fill& fill) {
  if (fill.isOpaque()) {
    // The clip is wide open, and the fill is opaque, so we can discard all previous records as
    // they are now invisible.
    clear();
  }
  if (fill.color.alpha > 0.0f) {
    recordStateAndFill({}, fill);
    auto record = blockBuffer.make<DrawFill>();
    records.emplace_back(std::move(record));
    drawCount++;
  }
}

void RecordingContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  recordStateAndFill(state, fill);
  auto record = blockBuffer.make<DrawRect>(rect);
  records.emplace_back(std::move(record));
  drawCount++;
}

void RecordingContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                                 const Stroke* stroke) {
  recordStateAndFill(state, fill);
  PlacementPtr<Record> record = nullptr;
  if (stroke) {
    record = blockBuffer.make<StrokeRRect>(rRect, *stroke);
  } else {
    record = blockBuffer.make<DrawRRect>(rRect);
  }
  records.emplace_back(std::move(record));
  drawCount++;
}

void RecordingContext::drawPath(const Path& path, const MCState& state, const Fill& fill) {
  recordStateAndFill(state, fill);
  auto record = blockBuffer.make<DrawPath>(path);
  records.emplace_back(std::move(record));
  drawCount++;
}

void RecordingContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                                 const Fill& fill) {
  DEBUG_ASSERT(shape != nullptr);
  recordStateAndFill(state, fill);
  auto record = blockBuffer.make<DrawShape>(std::move(shape));
  records.emplace_back(std::move(record));
  drawCount++;
}

void RecordingContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                     const SamplingOptions& sampling, const MCState& state,
                                     const Fill& fill, SrcRectConstraint constraint) {
  DEBUG_ASSERT(image != nullptr);
  recordStateAndFill(state, fill);
  auto imageRect = Rect::MakeWH(image->width(), image->height());
  PlacementPtr<Record> record = nullptr;
  if (rect == imageRect) {
    record = blockBuffer.make<DrawImage>(std::move(image), sampling);
  } else {
    record = blockBuffer.make<DrawImageRect>(std::move(image), rect, sampling,constraint);
  }
  records.emplace_back(std::move(record));
  drawCount++;
}

void RecordingContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                        const MCState& state, const Fill& fill,
                                        const Stroke* stroke) {
  recordStateAndFill(state, fill);
  PlacementPtr<Record> record = nullptr;
  if (stroke) {
    record = blockBuffer.make<StrokeGlyphRunList>(std::move(glyphRunList), *stroke);
  } else {
    record = blockBuffer.make<DrawGlyphRunList>(std::move(glyphRunList));
  }
  records.emplace_back(std::move(record));
  drawCount++;
}

void RecordingContext::drawLayer(std::shared_ptr<Picture> picture,
                                 std::shared_ptr<ImageFilter> filter, const MCState& state,
                                 const Fill& fill) {
  DEBUG_ASSERT(picture != nullptr);
  recordStateAndFill(state, fill);
  auto record = blockBuffer.make<DrawLayer>(std::move(picture), std::move(filter));
  records.emplace_back(std::move(record));
  drawCount++;
}

void RecordingContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  if (picture->drawCount > MaxPictureDrawsToUnrollInsteadOfReference) {
    recordState(state);
    auto record = blockBuffer.make<DrawPicture>(std::move(picture));
    records.emplace_back(std::move(record));
    drawCount++;
  } else {
    picture->playback(this, state);
  }
}

void RecordingContext::recordState(const tgfx::MCState& state) {
  if (lastState.matrix != state.matrix) {
    auto record = blockBuffer.make<SetMatrix>(state.matrix);
    records.emplace_back(std::move(record));
    lastState.matrix = state.matrix;
  }
  if (!lastState.clip.isSame(state.clip)) {
    auto record = blockBuffer.make<SetClip>(state.clip);
    records.emplace_back(std::move(record));
    lastState.clip = state.clip;
  }
}

static bool CompareFill(const Fill& a, const Fill& b) {
  // Ignore the color differences.
  return a.antiAlias == b.antiAlias && a.blendMode == b.blendMode && a.shader == b.shader &&
         a.maskFilter == b.maskFilter && a.colorFilter == b.colorFilter;
}

void RecordingContext::recordStateAndFill(const MCState& state, const Fill& fill) {
  recordState(state);
  if (!CompareFill(lastFill, fill)) {
    auto record = blockBuffer.make<SetFill>(fill);
    records.emplace_back(std::move(record));
    lastFill = fill;
  } else if (lastFill.color != fill.color) {
    auto record = blockBuffer.make<SetColor>(fill.color);
    records.emplace_back(std::move(record));
    lastFill.color = fill.color;
  }
}
}  // namespace tgfx
