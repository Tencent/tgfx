/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "core/PictureContext.h"
#include "utils/Log.h"
#include "utils/RectToRectMatrix.h"

namespace tgfx {
/**
 * This const is used to strike a balance between the speed of referencing a sub-picture into a
 * parent picture and the playback cost of recursing into the sub-picture to access its actual
 * operations. Currently, it is set to a conservatively small value. However, based on measurements
 * and other factors such as the type of operations contained, this value may need to be adjusted.
 */
constexpr int MaxPictureDrawsToUnrollInsteadOfReference = 1;

PictureContext::~PictureContext() {
  // make sure the records are cleared before the blockBuffer is destroyed.
  records.clear();
}

void PictureContext::clear() {
  records.clear();
  blockBuffer.clear();
  lastState = {};
  lastFill = {};
  lastStroke = {};
  hasStroke = false;
  drawCount = 0;
}

std::shared_ptr<Picture> PictureContext::finishRecordingAsPicture(bool shrinkToFit) {
  if (records.empty()) {
    return nullptr;
  }
  auto lastBlock = blockBuffer.currentBlock();
  auto blockData = blockBuffer.release();
  if (blockData == nullptr) {
    return nullptr;
  }
  if (shrinkToFit) {
    records.shrink_to_fit();
    auto oldBlockStart = reinterpret_cast<const uint8_t*>(lastBlock.first);
    auto oldBlockEnd = oldBlockStart + lastBlock.second;
    auto newBlock = blockData->shrinkLastBlockTo(lastBlock.second);
    if (newBlock != oldBlockStart) {
      for (auto it = records.rbegin(); it != records.rend(); ++it) {
        auto pointer = reinterpret_cast<const uint8_t*>(it->get());
        if (pointer >= oldBlockStart && pointer < oldBlockEnd) {
          it->remap(oldBlockStart, newBlock);
        } else {
          break;
        }
      }
    }
  }
  std::shared_ptr<Picture> picture =
      std::shared_ptr<Picture>(new Picture(std::move(blockData), std::move(records), drawCount));
  lastState = {};
  lastFill = {};
  lastStroke = {};
  hasStroke = false;
  drawCount = 0;
  return picture;
}

void PictureContext::drawFill(const Fill& fill) {
  if (fill.isOpaque()) {
    // The clip is wide open, and the fill is opaque, so we can discard all previous records as
    // they are now invisible.
    clear();
  }
  if (fill.color.alpha > 0.0f) {
    recordAll({}, fill);
    auto record = blockBuffer.make<DrawFill>();
    records.emplace_back(std::move(record));
    drawCount++;
  }
}

void PictureContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill,
                              const Stroke* stroke) {
  recordAll(state, fill, stroke);
  auto record = blockBuffer.make<DrawRect>(rect);
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                               const Stroke* stroke) {
  recordAll(state, fill, stroke);
  auto record = blockBuffer.make<DrawRRect>(rRect);
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawPath(const Path& path, const MCState& state, const Fill& fill) {
  recordAll(state, fill);
  auto record = blockBuffer.make<DrawPath>(path);
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill,
                               const Stroke* stroke) {
  DEBUG_ASSERT(shape != nullptr);
  recordAll(state, fill, stroke);
  auto record = blockBuffer.make<DrawShape>(std::move(shape));
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                               const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(image != nullptr);
  recordAll(state, fill);
  PlacementPtr<PictureRecord> record = nullptr;
  record = blockBuffer.make<DrawImage>(std::move(image), sampling);
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                   const Rect& dstRect, const SamplingOptions& sampling,
                                   const MCState& state, const Fill& fill,
                                   SrcRectConstraint constraint) {
  DEBUG_ASSERT(image != nullptr);
  auto newState = state;
  auto newFill = fill;
  bool needDstRect = true;
  if (srcRect.width() == dstRect.width() && srcRect.height() == dstRect.height()) {
    auto viewMatrix = MakeRectToRectMatrix(srcRect, dstRect);
    newState.matrix.preConcat(viewMatrix);
    Matrix fillMatrix = Matrix::I();
    viewMatrix.invert(&fillMatrix);
    newFill = newFill.makeWithMatrix(fillMatrix);
    needDstRect = false;
  }
  recordAll(newState, newFill);
  auto imageRect = Rect::MakeWH(image->width(), image->height());
  PlacementPtr<PictureRecord> record = nullptr;
  if (srcRect == imageRect && !needDstRect) {
    record = blockBuffer.make<DrawImage>(std::move(image), sampling);
  } else if (!needDstRect) {
    record = blockBuffer.make<DrawImageRect>(std::move(image), srcRect, sampling, constraint);
  } else {
    record = blockBuffer.make<DrawImageRectToRect>(std::move(image), srcRect, dstRect, sampling,
                                                   constraint);
  }
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                      const MCState& state, const Fill& fill,
                                      const Stroke* stroke) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  recordAll(state, fill, stroke);
  auto record = blockBuffer.make<DrawGlyphRunList>(std::move(glyphRunList));
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> filter, const MCState& state,
                               const Fill& fill) {
  DEBUG_ASSERT(picture != nullptr);
  recordAll(state, fill);
  auto record = blockBuffer.make<DrawLayer>(std::move(picture), std::move(filter));
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  if (picture->drawCount > MaxPictureDrawsToUnrollInsteadOfReference) {
    recordState(state);
    drawCount += picture->drawCount;
    auto record = blockBuffer.make<DrawPicture>(std::move(picture));
    records.emplace_back(std::move(record));
  } else {
    picture->playback(this, state);
  }
}

static bool CompareFill(const Fill& a, const Fill& b) {
  // Ignore the color differences.
  return a.antiAlias == b.antiAlias && a.blendMode == b.blendMode && a.shader == b.shader &&
         a.maskFilter == b.maskFilter && a.colorFilter == b.colorFilter;
}

void PictureContext::recordState(const MCState& state) {
  if (lastState.matrix != state.matrix) {
    auto record = blockBuffer.make<SetMatrix>(state.matrix);
    records.emplace_back(std::move(record));
    lastState.matrix = state.matrix;
  }
  if (lastState.clip != state.clip) {
    auto record = blockBuffer.make<SetClip>(state.clip);
    records.emplace_back(std::move(record));
    lastState.clip = state.clip;
  }
}

void PictureContext::recordFill(const Fill& fill) {
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

void PictureContext::recordStroke(const Stroke& stroke) {
  if (stroke.cap != lastStroke.cap || stroke.join != lastStroke.join ||
      stroke.miterLimit != lastStroke.miterLimit) {
    auto record = blockBuffer.make<SetStroke>(stroke);
    records.emplace_back(std::move(record));
    lastStroke = stroke;
  } else if (stroke.width != lastStroke.width) {
    auto record = blockBuffer.make<SetStrokeWidth>(stroke.width);
    records.emplace_back(std::move(record));
    lastStroke.width = stroke.width;
  } else if (!hasStroke) {
    auto record = blockBuffer.make<SetHasStroke>(true);
    records.emplace_back(std::move(record));
  }
  hasStroke = true;
}

void PictureContext::recordAll(const MCState& state, const Fill& fill, const Stroke* stroke) {
  recordState(state);
  recordFill(fill);
  if (stroke) {
    recordStroke(*stroke);
  } else if (hasStroke) {
    auto record = blockBuffer.make<SetHasStroke>(false);
    records.emplace_back(std::move(record));
    hasStroke = false;
  }
}
}  // namespace tgfx
