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
  invalidateContent();
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
  invalidateContent();
}

void Layer::setMask(std::shared_ptr<Layer> value) {
  if (_mask == value) {
    return;
  }
  _mask = std::move(value);
  invalidateContent();
}

void Layer::setScrollRect(const Rect* rect) {
  if (_scrollRect.get() == rect || (_scrollRect && rect && *_scrollRect == *rect)) {
    return;
  }
  if (rect) {
    _scrollRect = std::make_unique<Rect>(*rect);
  } else {
    _scrollRect = nullptr;
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
  invalidateContent();
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
  invalidateContent();
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
  invalidateContent();
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
  invalidateContent();
  return true;
}

Rect Layer::getBounds(const Layer* targetCoordinateSpace) const {
  auto totalMatrix = Matrix::I();
  if (targetCoordinateSpace) {
    auto rootLayer = this;
    while (rootLayer->_parent != nullptr && targetCoordinateSpace != rootLayer) {
      totalMatrix.preConcat(rootLayer->_matrix);
      rootLayer = rootLayer->_parent;
    }
    if (!rootLayer->doContains(targetCoordinateSpace)) {
      return Rect::MakeEmpty();
    }
    auto coordinateMatrix = Matrix::I();
    while (rootLayer != targetCoordinateSpace) {
      coordinateMatrix.preConcat(targetCoordinateSpace->_matrix);
      targetCoordinateSpace = targetCoordinateSpace->_parent;
    }
    if (!coordinateMatrix.invert(&coordinateMatrix)) {
      return Rect::MakeEmpty();
    }
    totalMatrix.postConcat(coordinateMatrix);
  }

  auto contentBounds = measureContentBounds();
  for (const auto& child : _children) {
    auto childBounds = child->getBounds();
    if (child->_scrollRect) {
      childBounds.intersect(*child->_scrollRect);
    }
    child->_matrix.mapRect(&childBounds);
    contentBounds.join(childBounds);
  }
  totalMatrix.mapRect(&contentBounds);

  return contentBounds;
}

Point Layer::globalToLocal(const Point& globalPoint) const {
  auto globalMatrix = getTotalMatrix();
  auto inverseMatrix = Matrix::I();
  if (!globalMatrix.invert(&inverseMatrix)) {
    return Point::Make(0, 0);
  }
  return inverseMatrix.mapXY(globalPoint.x, globalPoint.y);
}

Point Layer::localToGlobal(const Point& localPoint) const {
  auto globalMatrix = getTotalMatrix();
  return globalMatrix.mapXY(localPoint.x, localPoint.y);
}

bool Layer::hitTestPoint(float, float, bool) {
  return false;
}

void Layer::invalidate() {
  bitFields.dirty = true;
}

void Layer::invalidateContent() {
  invalidate();
  contentChange = true;
}

void Layer::draw(Canvas* canvas, float alpha, BlendMode blendMode) {
  if (alpha < 0 || canvas == nullptr) {
    return;
  }

  if (shouldRasterize() && !shouldUseCache()) {
    rasterizeContent(canvas->getSurface()->getContext());
  }

  Paint paint;
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  if (shouldUseCache()) {
    // draw the cached image
    canvas->save();
    canvas->concat(Matrix::MakeScale(1.0f / _rasterizationScale));
    canvas->drawImage(_owner->getCacheSurface(this)->makeImageSnapshot(), &paint);
    canvas->restore();
  } else if (blendMode != BlendMode::SrcOver || (alpha < 1 && bitFields.allowsGroupOpacity)) {
    // draw with recoder
    Recorder recorder;
    auto contentCanvas = recorder.beginRecording();
    drawContent(contentCanvas, 1.0f);
    canvas->drawPicture(recorder.finishRecordingAsPicture(), nullptr, &paint);
  } else {
    // draw directly
    drawContent(canvas, alpha);
  }
}

void Layer::rasterizeContent(Context* context) {
  Rect bounds = getBounds();
  bounds.scale(_rasterizationScale, _rasterizationScale);

  auto surfaceWidth = static_cast<int>(bounds.width());
  auto surfaceHeight = static_cast<int>(bounds.height());

  auto cacheSurface = _owner ? _owner->getCacheSurface(this) : nullptr;
  if (cacheSurface == nullptr || cacheSurface->width() != surfaceWidth ||
      cacheSurface->height() != surfaceHeight) {
    cacheSurface = Surface::Make(context, surfaceWidth, surfaceHeight);
  }

  auto cacheCanvas = cacheSurface->getCanvas();
  if (!cacheCanvas) {
    return;
  }
  cacheCanvas->concat(Matrix::MakeScale(_rasterizationScale));
  drawContent(cacheCanvas, 1);

  if (_owner) {
    _owner->setCacheSurface(this, cacheSurface);
  }
}

void Layer::drawContent(Canvas* canvas, float alpha) {
  onDraw(canvas, alpha);
  for (const auto& child : _children) {
    drawChild(canvas, alpha, child.get());
  }
  dirty = false;
  contentChange = false;
}

void Layer::onAttachToDisplayList(DisplayList* owner) {
  _owner = owner;
  for (auto child : _children) {
    child->onAttachToDisplayList(owner);
  }
}

void Layer::onDetachFromDisplayList() {
  if (_owner) {
    _owner->setCacheSurface(this, nullptr);
  }
  _owner = nullptr;
  for (auto child : _children) {
    child->onDetachFromDisplayList();
  }
}

void Layer::drawChild(Canvas* canvas, float alpha, Layer* child) {
  if (!child->visible() || child->_alpha <= 0) {
    return;
  }
  canvas->save();
  canvas->concat(child->_matrix);
  if (child->_scrollRect) {
    canvas->clipRect(*child->_scrollRect);
  }
  child->draw(canvas, child->_alpha * alpha, child->_blendMode);
  canvas->restore();
}

int Layer::doGetChildIndex(const Layer* child) const {
  for (size_t i = 0; i < _children.size(); ++i) {
    if (_children[i].get() == child) {
      return static_cast<int>(i);
    }
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

bool Layer::shouldUseCache() const {
  return !contentChange && _owner->hasCache(this);
}

void Layer::onDraw(Canvas*, float) {
}

Rect Layer::measureContentBounds() const {
  return Rect::MakeEmpty();
}

Matrix Layer::getTotalMatrix() const {
  auto totalMatrix = Matrix::I();
  auto layer = this;
  while (layer) {
    totalMatrix = layer->_matrix * totalMatrix;
    layer = layer->_parent;
  }
  return totalMatrix;
}
}  // namespace tgfx
