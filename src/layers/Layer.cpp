/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/layers/Layer.h"
#include <atomic>
#include "core/utils/Log.h"
#include "layers/DrawArgs.h"
#include "layers/contents/RasterizedContent.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/DisplayList.h"

namespace tgfx {
static std::atomic_bool AllowsEdgeAntialiasing = true;
static std::atomic_bool AllowsGroupOpacity = false;

bool Layer::DefaultAllowsEdgeAntialiasing() {
  return AllowsEdgeAntialiasing;
}

void Layer::SetDefaultAllowsEdgeAntialiasing(bool value) {
  AllowsEdgeAntialiasing = value;
}

bool Layer::DefaultAllowsGroupOpacity() {
  return AllowsGroupOpacity;
}

void Layer::SetDefaultAllowsGroupOpacity(bool value) {
  AllowsGroupOpacity = value;
}

std::shared_ptr<Layer> Layer::Make() {
  auto layer = std::shared_ptr<Layer>(new Layer());
  layer->weakThis = layer;
  return layer;
}

Layer::Layer()
    : bitFields({
          true,                    //dirty
          false,                   //contentDirty
          false,                   //childrenDirty
          true,                    //visible
          false,                   //shouldRasterize
          AllowsEdgeAntialiasing,  //allowsEdgeAntialiasing
          AllowsGroupOpacity,      //allowsGroupOpacity
      }) {
}

void Layer::setAlpha(float value) {
  if (_alpha == value) {
    return;
  }
  _alpha = value;
  invalidate();
}

void Layer::setBlendMode(BlendMode value) {
  if (_blendMode == value) {
    return;
  }
  _blendMode = value;
  invalidate();
}

void Layer::setPosition(const Point& value) {
  if (_matrix.getTranslateX() == value.x && _matrix.getTranslateY() == value.y) {
    return;
  }
  _matrix.setTranslateX(value.x);
  _matrix.setTranslateY(value.y);
  invalidate();
}

void Layer::setMatrix(const Matrix& value) {
  if (_matrix == value) {
    return;
  }
  _matrix = value;
  invalidate();
}

void Layer::setVisible(bool value) {
  if (bitFields.visible == value) {
    return;
  }
  bitFields.visible = value;
  invalidate();
}

void Layer::setShouldRasterize(bool value) {
  if (bitFields.shouldRasterize == value) {
    return;
  }
  bitFields.shouldRasterize = value;
  invalidate();
}

void Layer::setRasterizationScale(float value) {
  if (_rasterizationScale == value) {
    return;
  }
  _rasterizationScale = value;
  invalidate();
}

void Layer::setAllowsEdgeAntialiasing(bool value) {
  if (bitFields.allowsEdgeAntialiasing == value) {
    return;
  }
  bitFields.allowsEdgeAntialiasing = value;
  invalidate();
}

void Layer::setAllowsGroupOpacity(bool value) {
  if (bitFields.allowsGroupOpacity == value) {
    return;
  }
  bitFields.allowsGroupOpacity = value;
  invalidate();
}

void Layer::setFilters(std::vector<std::shared_ptr<LayerFilter>> value) {
  if (_filters.size() == value.size() &&
      std::equal(_filters.begin(), _filters.end(), value.begin())) {
    return;
  }
  _filters = std::move(value);
  rasterizedContent = nullptr;
  invalidate();
}

void Layer::setMask(std::shared_ptr<Layer> value) {
  if (_mask == value) {
    return;
  }
  _mask = std::move(value);
  invalidate();
}

void Layer::setScrollRect(const Rect& rect) {
  if ((_scrollRect && *_scrollRect == rect) || (!_scrollRect && rect.isEmpty())) {
    return;
  }
  if (rect.isEmpty()) {
    _scrollRect = nullptr;
  } else {
    _scrollRect = std::make_unique<Rect>(rect);
  }
  invalidate();
}

bool Layer::addChild(std::shared_ptr<Layer> child) {
  if (!child) {
    return false;
  }
  auto index = _children.size();
  if (child->parent().get() == this) {
    index--;
  }
  return addChildAt(child, static_cast<int>(index));
}

bool Layer::addChildAt(std::shared_ptr<Layer> child, int index) {
  if (!child) {
    return false;
  }
  if (child.get() == this) {
    LOGE("addChildAt() The child is the same as the parent.");
    return false;
  } else if (child->doContains(this)) {
    LOGE("addChildAt() The child is already a parent of the parent.");
    return false;
  } else if (child->_owner && child->_owner->root() == child.get()) {
    LOGE("A root layer cannot be added as a child to another layer.");
    return false;
  }
  if (child->parent().get() == this) {
    return setChildIndex(child, index);
  }
  child->removeFromParent();
  _children.insert(_children.begin() + index, child);
  child->_parent = this;
  child->onAttachToDisplayList(_owner);
  invalidateChildren();
  return true;
}

bool Layer::contains(std::shared_ptr<Layer> child) const {
  return doContains(child.get());
}

std::shared_ptr<Layer> Layer::getChildByName(const std::string& name) {
  for (const auto& child : _children) {
    if (child->name() == name) {
      return child;
    }
  }
  return nullptr;
}

int Layer::getChildIndex(std::shared_ptr<Layer> child) const {
  return doGetChildIndex(child.get());
}

std::vector<std::shared_ptr<Layer>> Layer::getLayersUnderPoint(float, float) {
  return {};
}

void Layer::removeFromParent() {
  if (!_parent) {
    return;
  }
  _parent->removeChildAt(_parent->doGetChildIndex(this));
}

std::shared_ptr<Layer> Layer::removeChildAt(int index) {
  if (index < 0 || static_cast<size_t>(index) >= _children.size()) {
    LOGE("The supplied index is out of bounds.");
    return nullptr;
  }
  auto child = _children[static_cast<size_t>(index)];
  child->_parent = nullptr;
  child->onDetachFromDisplayList();
  _children.erase(_children.begin() + index);
  invalidateChildren();
  return child;
}

void Layer::removeChildren(int beginIndex, int endIndex) {
  if (beginIndex < 0 || static_cast<size_t>(beginIndex) >= _children.size()) {
    LOGE("The supplied beginIndex is out of bounds.");
    return;
  }
  if (endIndex < 0 || static_cast<size_t>(endIndex) >= _children.size()) {
    endIndex = static_cast<int>(_children.size()) - 1;
  }
  for (int i = endIndex; i >= beginIndex; --i) {
    removeChildAt(i);
  }
}

bool Layer::setChildIndex(std::shared_ptr<Layer> child, int index) {
  if (index < 0 || static_cast<size_t>(index) >= _children.size()) {
    index = static_cast<int>(_children.size()) - 1;
  }
  auto oldIndex = getChildIndex(child);
  if (oldIndex < 0) {
    LOGE("The supplied layer must be a child layer of the caller.");
    return false;
  }
  if (oldIndex == index) {
    return true;
  }
  _children.erase(_children.begin() + oldIndex);
  _children.insert(_children.begin() + index, child);
  invalidateChildren();
  return true;
}

bool Layer::replaceChild(std::shared_ptr<Layer> oldChild, std::shared_ptr<Layer> newChild) {
  auto index = getChildIndex(oldChild);
  if (index < 0) {
    LOGE("The supplied layer must be a child layer of the caller.");
    return false;
  }
  if (!addChildAt(newChild, index)) {
    return false;
  }
  oldChild->removeFromParent();
  invalidateChildren();
  return true;
}

Rect Layer::getBounds(const Layer* targetCoordinateSpace) {
  Rect bounds = Rect::MakeEmpty();
  auto content = getContent();
  if (content) {
    bounds.join(content->getBounds());
  }
  for (const auto& child : _children) {
    auto childBounds = child->getBounds();
    if (child->_scrollRect) {
      if (!childBounds.intersect(*child->_scrollRect)) {
        continue;
      }
    }
    child->getMatrixWithScrollRect().mapRect(&childBounds);
    bounds.join(childBounds);
  }

  if (targetCoordinateSpace && targetCoordinateSpace != this) {
    auto totalMatrix = getGlobalMatrix();
    auto coordinateMatrix = targetCoordinateSpace->getGlobalMatrix();
    if (!coordinateMatrix.invert(&coordinateMatrix)) {
      return Rect::MakeEmpty();
    }
    totalMatrix.postConcat(coordinateMatrix);
    totalMatrix.mapRect(&bounds);
  }

  return bounds;
}

Point Layer::globalToLocal(const Point& globalPoint) const {
  auto globalMatrix = getGlobalMatrix();
  auto inverseMatrix = Matrix::I();
  if (!globalMatrix.invert(&inverseMatrix)) {
    return Point::Make(0, 0);
  }
  return inverseMatrix.mapXY(globalPoint.x, globalPoint.y);
}

Point Layer::localToGlobal(const Point& localPoint) const {
  auto globalMatrix = getGlobalMatrix();
  return globalMatrix.mapXY(localPoint.x, localPoint.y);
}

bool Layer::hitTestPoint(float, float, bool) {
  return false;
}

void Layer::draw(Canvas* canvas, float alpha, BlendMode blendMode) {
  if (canvas == nullptr || alpha <= 0) {
    return;
  }
  auto surface = canvas->getSurface();
  DrawArgs args = {};
  if (surface) {
    args.context = surface->getContext();
    args.renderFlags = surface->renderFlags();
  }
  drawLayer(args, canvas, alpha, blendMode);
}

void Layer::invalidate() {
  if (bitFields.dirty) {
    return;
  }
  bitFields.dirty = true;
  if (_parent) {
    _parent->invalidateChildren();
  }
}

void Layer::invalidateContent() {
  if (bitFields.contentDirty) {
    return;
  }
  bitFields.contentDirty = true;
  rasterizedContent = nullptr;
  invalidate();
}

void Layer::invalidateChildren() {
  if (bitFields.childrenDirty) {
    return;
  }
  bitFields.childrenDirty = true;
  rasterizedContent = nullptr;
  invalidate();
}

std::unique_ptr<LayerContent> Layer::onUpdateContent() {
  return nullptr;
}

void Layer::onAttachToDisplayList(DisplayList* owner) {
  _owner = owner;
  for (auto& child : _children) {
    child->onAttachToDisplayList(owner);
  }
}

void Layer::onDetachFromDisplayList() {
  _owner = nullptr;
  for (auto& child : _children) {
    child->onDetachFromDisplayList();
  }
}

int Layer::doGetChildIndex(const Layer* child) const {
  int index = 0;
  for (auto& layer : _children) {
    if (layer.get() == child) {
      return index;
    }
    index++;
  }
  return -1;
}

bool Layer::doContains(const Layer* child) const {
  auto target = child;
  while (target) {
    if (target == this) {
      return true;
    }
    target = target->_parent;
  }
  return false;
}

Matrix Layer::getGlobalMatrix() const {
  // The root layer is the root of the display list, so we don't need to include it in the matrix.
  auto root = owner() ? owner()->root() : nullptr;
  auto matrix = Matrix::I();
  auto layer = this;
  while (layer != root) {
    matrix.postConcat(layer->getMatrixWithScrollRect());
    layer = layer->_parent;
  }
  return matrix;
}

Matrix Layer::getMatrixWithScrollRect() const {
  auto matrix = _matrix;
  if (_scrollRect) {
    matrix.preTranslate(-_scrollRect->left, -_scrollRect->top);
  }
  return matrix;
}

LayerContent* Layer::getContent() {
  if (bitFields.contentDirty) {
    layerContent = onUpdateContent();
    bitFields.contentDirty = false;
  }
  return layerContent.get();
}

Paint Layer::getLayerPaint(float alpha, BlendMode blendMode) {
  Paint paint;
  paint.setAntiAlias(bitFields.allowsEdgeAntialiasing);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  return paint;
}

LayerContent* Layer::getRasterizedCache(const DrawArgs& args) {
  if (!bitFields.shouldRasterize || args.context == nullptr) {
    return nullptr;
  }
  auto contextID = args.context->uniqueID();
  auto content = static_cast<RasterizedContent*>(rasterizedContent.get());
  if (content && content->contextID() == contextID) {
    return content;
  }
  if (args.renderFlags & RenderFlags::DisableCache) {
    return nullptr;
  }
  Rect bounds = getBounds();
  auto width = roundf(bounds.width() * _rasterizationScale);
  auto height = roundf(bounds.height() * _rasterizationScale);
  Matrix matrix = Matrix::MakeScale(width / bounds.width(), height / bounds.height());
  matrix.preTranslate(-bounds.left, -bounds.top);
  Matrix drawingMatrix = {};
  if (!matrix.invert(&drawingMatrix)) {
    return nullptr;
  }
  auto renderFlags = args.renderFlags | RenderFlags::DisableCache;
  auto surface = Surface::Make(args.context, static_cast<int>(width), static_cast<int>(height),
                               false, 1, false, renderFlags);
  if (surface == nullptr) {
    return nullptr;
  }
  auto canvas = surface->getCanvas();
  canvas->concat(matrix);
  DrawArgs drawArgs(args.context, renderFlags, true);
  drawContents(drawArgs, canvas, 1.0f);
  auto image = surface->makeImageSnapshot();
  rasterizedContent = std::make_unique<RasterizedContent>(contextID, image, drawingMatrix);
  return rasterizedContent.get();
}

void Layer::drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode) {
  DEBUG_ASSERT(canvas != nullptr);
  auto rasterizedCache = getRasterizedCache(args);
  Paint paint;
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  if (rasterizedCache) {
    rasterizedCache->draw(canvas, paint);
  } else if (blendMode != BlendMode::SrcOver || (alpha < 1.0f && bitFields.allowsGroupOpacity)) {
    Recorder recorder;
    auto contentCanvas = recorder.beginRecording();
    drawContents(args, contentCanvas, 1.0f);
    canvas->drawPicture(recorder.finishRecordingAsPicture(), nullptr, &paint);
  } else {
    // draw directly
    drawContents(args, canvas, alpha);
  }
  if (args.cleanDirtyFlags) {
    bitFields.dirty = false;
  }
}

void Layer::drawContents(const DrawArgs& args, Canvas* canvas, float alpha) {
  auto content = getContent();
  if (content) {
    content->draw(canvas, getLayerPaint(alpha, BlendMode::SrcOver));
  }
  for (const auto& child : _children) {
    if (!child->visible() || child->_alpha <= 0) {
      continue;
    }
    canvas->save();
    canvas->concat(child->getMatrixWithScrollRect());
    if (child->_scrollRect) {
      canvas->clipRect(Rect::MakeWH(child->_scrollRect->width(), child->_scrollRect->height()));
    }
    child->drawLayer(args, canvas, child->_alpha * alpha, child->_blendMode);
    canvas->restore();
  }
  if (args.cleanDirtyFlags) {
    bitFields.childrenDirty = false;
  }
}

}  // namespace tgfx
