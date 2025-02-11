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
#include "core/images/PictureImage.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/DrawArgs.h"
#include "layers/contents/RasterizedContent.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"

namespace tgfx {
static std::atomic_bool AllowsEdgeAntialiasing = true;
static std::atomic_bool AllowsGroupOpacity = false;

struct LayerStyleSource {
  float contentScale = 1.0f;
  std::shared_ptr<Image> content = nullptr;
  Point contentOffset = Point::Zero();
  std::shared_ptr<Image> contour = nullptr;
  Point contourOffset = Point::Zero();
  std::shared_ptr<Image> background = nullptr;
  Point backgroundOffset = Point::Zero();
};

static std::shared_ptr<Picture> CreatePicture(
    const DrawArgs& args, float contentScale,
    const std::function<void(const DrawArgs&, Canvas*)>& drawFunction) {
  if (drawFunction == nullptr) {
    return nullptr;
  }
  Recorder recorder = {};
  auto contentCanvas = recorder.beginRecording();
  contentCanvas->scale(contentScale, contentScale);
  drawFunction(args, contentCanvas);
  return recorder.finishRecordingAsPicture();
}

static std::shared_ptr<Image> CreatePictureImage(std::shared_ptr<Picture> picture, Point* offset) {
  if (picture == nullptr) {
    return nullptr;
  }
  auto bounds = picture->getBounds();
  bounds.roundOut();
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(std::move(picture), static_cast<int>(bounds.width()),
                               static_cast<int>(bounds.height()), &matrix);
  if (offset) {
    *offset = Point::Make(bounds.x(), bounds.y());
  }
  return image;
}

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

Layer::~Layer() {
  for (const auto& filter : _filters) {
    filter->detachFromLayer(this);
  }
  if (_mask) {
    _mask->maskOwner = nullptr;
  }
  removeChildren();
}

Layer::Layer() {
  memset(&bitFields, 0, sizeof(bitFields));
  bitFields.visible = true;
  bitFields.allowsEdgeAntialiasing = AllowsEdgeAntialiasing;
  bitFields.allowsGroupOpacity = AllowsGroupOpacity;
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
  if (_rasterizationScale < 0) {
    _rasterizationScale = 0;
  }
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
  for (const auto& filter : _filters) {
    filter->detachFromLayer(this);
  }
  _filters = std::move(value);
  for (const auto& filter : _filters) {
    filter->attachToLayer(this);
  }
  rasterizedContent = nullptr;
  invalidate();
}

void Layer::setMask(std::shared_ptr<Layer> value) {
  if (_mask.get() == this) {
    return;
  }
  if (_mask == value) {
    return;
  }
  if (value && value->maskOwner) {
    value->maskOwner->setMask(nullptr);
    value->maskOwner = nullptr;
  }
  if (_mask) {
    _mask->maskOwner = nullptr;
  }
  _mask = std::move(value);
  if (_mask) {
    _mask->maskOwner = this;
  }
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

void Layer::setLayerStyles(const std::vector<std::shared_ptr<LayerStyle>>& value) {
  if (_layerStyles.size() == value.size() &&
      std::equal(_layerStyles.begin(), _layerStyles.end(), value.begin())) {
    return;
  }
  for (const auto& layerStyle : _layerStyles) {
    layerStyle->detachFromLayer(this);
  }
  _layerStyles = std::move(value);
  for (const auto& layerStyle : _layerStyles) {
    layerStyle->attachToLayer(this);
  }
  rasterizedContent = nullptr;
  invalidate();
}

void Layer::setExcludeChildEffectsInLayerStyle(bool value) {
  if (value == bitFields.excludeChildEffectsInLayerStyle) {
    return;
  }
  bitFields.excludeChildEffectsInLayerStyle = value;
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
  } else if (child->_root == child.get()) {
    LOGE("A root layer cannot be added as a child to another layer.");
    return false;
  }
  if (child->parent().get() == this) {
    return setChildIndex(child, index);
  }
  child->removeFromParent();
  _children.insert(_children.begin() + index, child);
  child->_parent = this;
  child->onAttachToRoot(_root);
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

std::vector<std::shared_ptr<Layer>> Layer::getLayersUnderPoint(float x, float y) {
  std::vector<std::shared_ptr<Layer>> results;
  getLayersUnderPointInternal(x, y, &results);
  return results;
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
  child->onDetachFromRoot();
  _children.erase(_children.begin() + index);
  invalidateChildren();
  return child;
}

void Layer::removeChildren(int beginIndex, int endIndex) {
  if (_children.empty()) {
    return;
  }
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
    if (!child->visible() || child->maskOwner) {
      continue;
    }
    auto childBounds = child->getBounds();
    if (child->_scrollRect) {
      if (!childBounds.intersect(*child->_scrollRect)) {
        continue;
      }
    }
    if (child->hasValidMask()) {
      auto relativeMatrix = child->_mask->getRelativeMatrix(child.get());
      auto maskBounds = child->_mask->getBounds();
      auto maskBoundsToCurrentLayer = relativeMatrix.mapRect(maskBounds);
      if (!childBounds.intersect(maskBoundsToCurrentLayer)) {
        continue;
      }
    }
    child->getMatrixWithScrollRect().mapRect(&childBounds);

    bounds.join(childBounds);
  }

  if (!_layerStyles.empty()) {
    auto layerBounds = bounds;
    for (auto& layerStyle : _layerStyles) {
      auto styleBounds = layerStyle->filterBounds(layerBounds, 1.0f);
      bounds.join(styleBounds);
    }
  }

  auto filter = getImageFilter(1.0f);
  if (filter) {
    bounds = filter->filterBounds(bounds);
  }

  if (targetCoordinateSpace && targetCoordinateSpace != this) {
    auto relativeMatrix = getRelativeMatrix(targetCoordinateSpace);
    relativeMatrix.mapRect(&bounds);
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

bool Layer::hitTestPoint(float x, float y, bool pixelHitTest) {
  auto content = getContent();
  if (nullptr != content) {
    Point localPoint = globalToLocal(Point::Make(x, y));
    if (content->hitTestPoint(localPoint.x, localPoint.y, pixelHitTest)) {
      return true;
    }
  }

  for (const auto& childLayer : _children) {
    if (!childLayer->visible() || childLayer->_alpha <= 0.f || childLayer->maskOwner) {
      continue;
    }

    if (nullptr != childLayer->_scrollRect) {
      auto pointInChildSpace = childLayer->globalToLocal(Point::Make(x, y));
      if (!childLayer->_scrollRect->contains(pointInChildSpace.x, pointInChildSpace.y)) {
        continue;
      }
    }

    if (nullptr != childLayer->_mask) {
      if (!childLayer->_mask->hitTestPoint(x, y, pixelHitTest)) {
        continue;
      }
    }

    if (childLayer->hitTestPoint(x, y, pixelHitTest)) {
      return true;
    }
  }

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
  if (maskOwners.empty()) {
    if (_parent) {
      _parent->invalidateChildren();
    }
  } else {
    for (auto maskOwner : maskOwners) {
      maskOwner->invalidate();
    }
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

void Layer::attachProperty(LayerProperty* property) const {
  if (property) {
    property->attachToLayer(this);
  }
}

void Layer::detachProperty(LayerProperty* property) const {
  if (property) {
    property->detachFromLayer(this);
  }
}

void Layer::onAttachToRoot(Layer* owner) {
  _root = owner;
  for (auto& child : _children) {
    child->onAttachToRoot(owner);
  }
}

void Layer::onDetachFromRoot() {
  _root = nullptr;
  for (auto& child : _children) {
    child->onDetachFromRoot();
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
  // The global matrix transforms the layer's local coordinate space to the coordinate space of its
  // top-level parent layer. This means the top-level parent layer's own matrix is not included in
  // the global matrix.
  auto matrix = Matrix::I();
  auto layer = this;
  while (layer->_parent) {
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

Paint Layer::getLayerPaint(float alpha, BlendMode blendMode) const {
  Paint paint = {};
  paint.setAntiAlias(bitFields.allowsEdgeAntialiasing);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  return paint;
}

std::shared_ptr<ImageFilter> Layer::getImageFilter(float contentScale) {
  if (_filters.empty()) {
    return nullptr;
  }
  std::vector<std::shared_ptr<ImageFilter>> filters;
  for (const auto& layerFilter : _filters) {
    if (auto filter = layerFilter->getImageFilter(contentScale)) {
      filters.push_back(filter);
    }
  }
  return ImageFilter::Compose(filters);
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
  auto drawingMatrix = Matrix::I();
  auto image = getRasterizedImage(args, _rasterizationScale, &drawingMatrix);
  if (image == nullptr) {
    return nullptr;
  }
  image = image->makeTextureImage(args.context);
  if (image == nullptr) {
    return nullptr;
  }
  rasterizedContent =
      std::make_unique<RasterizedContent>(contextID, std::move(image), drawingMatrix);
  return rasterizedContent.get();
}

std::shared_ptr<Image> Layer::getRasterizedImage(const DrawArgs& args, float contentScale,
                                                 Matrix* drawingMatrix) {
  DEBUG_ASSERT(drawingMatrix != nullptr);
  if (FloatNearlyZero(contentScale)) {
    return nullptr;
  }
  auto picture = CreatePicture(args, contentScale, [this](const DrawArgs& args, Canvas* canvas) {
    drawDirectly(args, canvas, 1.0f);
  });
  if (!picture) {
    return nullptr;
  }
  Point offset = Point::Zero();
  auto image = CreatePictureImage(std::move(picture), &offset);
  if (image == nullptr) {
    return nullptr;
  }
  auto filter = getImageFilter(contentScale);
  if (filter) {
    auto filterOffset = Point::Zero();
    image = image->makeWithFilter(std::move(filter), &filterOffset);
    offset += filterOffset;
  }
  drawingMatrix->setScale(1.0f / contentScale, 1.0f / contentScale);
  drawingMatrix->preTranslate(offset.x, offset.y);
  return image;
}

void Layer::drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode) {
  DEBUG_ASSERT(canvas != nullptr);
  if (auto rasterizedCache = getRasterizedCache(args)) {
    rasterizedCache->draw(canvas, getLayerPaint(alpha, blendMode));
  } else if (blendMode != BlendMode::SrcOver || (alpha < 1.0f && allowsGroupOpacity()) ||
             (!_filters.empty() && !args.excludeEffects) || hasValidMask()) {
    drawOffscreen(args, canvas, alpha, blendMode);
  } else {
    // draw directly
    drawDirectly(args, canvas, alpha);
  }
}

Matrix Layer::getRelativeMatrix(const Layer* targetCoordinateSpace) const {
  if (targetCoordinateSpace == nullptr || targetCoordinateSpace == this) {
    return Matrix::I();
  }
  auto targetLayerMatrix = targetCoordinateSpace->getGlobalMatrix();
  Matrix targetLayerInverseMatrix = Matrix::I();
  if (!targetLayerMatrix.invert(&targetLayerInverseMatrix)) {
    return Matrix::I();
  }
  Matrix relativeMatrix = getGlobalMatrix();
  relativeMatrix.postConcat(targetLayerInverseMatrix);
  return relativeMatrix;
}

std::shared_ptr<MaskFilter> Layer::getMaskFilter(const DrawArgs& args, float scale) {
  auto maskPicture = CreatePicture(args, scale, [this](const DrawArgs& args, Canvas* canvas) {
    _mask->drawLayer(args, canvas, _mask->_alpha, BlendMode::SrcOver);
  });
  if (maskPicture == nullptr) {
    return nullptr;
  }
  auto maskImageOffset = Point::Zero();
  auto maskContentImage = CreatePictureImage(maskPicture, &maskImageOffset);
  if (maskContentImage == nullptr) {
    return nullptr;
  }
  auto relativeMatrix = _mask->getRelativeMatrix(this);
  relativeMatrix.preScale(1.0f / scale, 1.0f / scale);
  relativeMatrix.preTranslate(maskImageOffset.x, maskImageOffset.y);
  relativeMatrix.postScale(scale, scale);
  auto shader = Shader::MakeImageShader(maskContentImage, TileMode::Decal, TileMode::Decal);
  if (shader) {
    shader = shader->makeWithMatrix(relativeMatrix);
  }
  return MaskFilter::MakeShader(shader);
}

void Layer::drawOffscreen(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode) {
  auto contentScale = canvas->getMatrix().getMaxScale();
  if (FloatNearlyZero(contentScale)) {
    return;
  }

  auto paint = getLayerPaint(alpha, blendMode);
  if (hasValidMask()) {
    auto maskFilter = getMaskFilter(args, contentScale);
    // if mask filter is nullptr while mask is valid, that means the layer is not visible.
    if (!maskFilter) {
      return;
    }
    paint.setMaskFilter(maskFilter);
  }

  auto picture = CreatePicture(args, contentScale, [this](const DrawArgs& args, Canvas* canvas) {
    drawDirectly(args, canvas, 1.0f);
  });
  if (picture == nullptr) {
    return;
  }
  if (!args.excludeEffects) {
    paint.setImageFilter(getImageFilter(contentScale));
  }
  auto matrix = Matrix::MakeScale(1.0f / contentScale);
  canvas->drawPicture(std::move(picture), &matrix, &paint);
}

void Layer::drawDirectly(const DrawArgs& args, Canvas* canvas, float alpha) {
  auto layerStyleSource = getLayerStyleSource(args, canvas->getMatrix());
  if (layerStyleSource) {
    drawLayerStyles(canvas, alpha, layerStyleSource.get(), LayerStylePosition::Below);
  }
  drawContents(getContent(), canvas, alpha, args.drawMode == DrawMode::Contour, [&]() {
    drawChildren(args, canvas, alpha);
    if (layerStyleSource) {
      drawLayerStyles(canvas, alpha, layerStyleSource.get(), LayerStylePosition::Above);
    }
    return true;
  });
}

void Layer::drawContents(LayerContent* content, Canvas* canvas, float alpha, bool,
                         const std::function<bool()>& drawChildren) const {
  if (content != nullptr) {
    content->draw(canvas, getLayerPaint(alpha));
  }
  drawChildren();
}

bool Layer::drawChildren(const DrawArgs& args, Canvas* canvas, float alpha, Layer* stopChild) {
  for (const auto& child : _children) {
    if (child.get() == stopChild) {
      return false;
    }
    if (!child->visible() || child->_alpha <= 0 || child->maskOwner) {
      continue;
    }
    AutoCanvasRestore autoRestore(canvas);
    canvas->concat(child->getMatrixWithScrollRect());
    if (child->_scrollRect) {
      canvas->clipRect(*child->_scrollRect);
    }
    child->drawLayer(args, canvas, child->_alpha * alpha, child->_blendMode);
  }
  if (args.cleanDirtyFlags) {
    bitFields.childrenDirty = false;
  }
  return true;
}

void Layer::drawBackground(const DrawArgs& args, Canvas* canvas, float* contentAlpha) {
  float alpha = 1.0f;
  if (contentAlpha == nullptr) {
    contentAlpha = &alpha;
  }

  // parent background -> parent content -> sibling nodes content -> current layer styles (drop shadow)
  if (_parent != nullptr) {
    _parent->drawBackground(args, canvas, contentAlpha);
    _parent->drawContents(_parent->getContent(), canvas, *contentAlpha, false, [&]() {
      return _parent->drawChildren(args, canvas, *contentAlpha, this);
    });
    *contentAlpha = *contentAlpha * _alpha;
    canvas->concat(getMatrixWithScrollRect());
    if (_scrollRect) {
      canvas->clipRect(*_scrollRect);
    }
  }

  // draw layer styles below the layer content. While drawing the background, args while be set to
  // DrawType::Background, and layerStyleSource->background will be nullptr.
  auto layerStyleSource = getLayerStyleSource(args, canvas->getMatrix());
  if (layerStyleSource) {
    drawLayerStyles(canvas, *contentAlpha, layerStyleSource.get(), LayerStylePosition::Below);
  }
}

std::unique_ptr<LayerStyleSource> Layer::getLayerStyleSource(const DrawArgs& args,
                                                             const Matrix& matrix) {
  if (_layerStyles.empty() || args.excludeEffects) {
    return nullptr;
  }
  auto contentScale = matrix.getMaxScale();
  if (FloatNearlyZero(contentScale)) {
    return nullptr;
  }

  auto drawLayerContents = [this](const DrawArgs& drawArgs, Canvas* canvas) {
    drawContents(getContent(), canvas, 1.0f, drawArgs.drawMode == DrawMode::Contour,
                 [&]() { return drawChildren(drawArgs, canvas, 1.0f); });
  };

  DrawArgs drawArgs(args.context, args.renderFlags | RenderFlags::DisableCache, false,
                    bitFields.excludeChildEffectsInLayerStyle);
  auto contentPicture = CreatePicture(drawArgs, contentScale, drawLayerContents);
  auto contentOffset = Point::Zero();
  auto content = CreatePictureImage(contentPicture, &contentOffset);
  if (content == nullptr) {
    return nullptr;
  }
  auto source = std::make_unique<LayerStyleSource>();
  source->contentScale = contentScale;
  source->content = std::move(content);
  source->contentOffset = contentOffset;

  auto needContour =
      std::any_of(_layerStyles.begin(), _layerStyles.end(), [](const auto& layerStyle) {
        return layerStyle->extraSourceType() == LayerStyleExtraSourceType::Contour;
      });
  if (needContour) {
    // Child effects are always excluded when drawing the layer contour.
    drawArgs.excludeEffects = true;
    drawArgs.drawMode = DrawMode::Contour;
    auto contourPicture = CreatePicture(drawArgs, contentScale, drawLayerContents);
    source->contour = CreatePictureImage(contourPicture, &source->contourOffset);
  }

  auto needBackground =
      args.drawMode != DrawMode::Background &&
      std::any_of(_layerStyles.begin(), _layerStyles.end(), [](const auto& layerStyle) {
        return layerStyle->extraSourceType() == LayerStyleExtraSourceType::Background;
      });
  if (needBackground) {
    //effects are always included when drawing the background.
    drawArgs.excludeEffects = false;
    drawArgs.drawMode = DrawMode::Background;
    auto backgroundDrawer = [this](const DrawArgs& args, Canvas* canvas) {
      auto bounds = getBounds();
      canvas->clipRect(bounds);
      auto globalMatrix = getGlobalMatrix();
      auto invertMatrix = Matrix::I();
      if (!globalMatrix.invert(&invertMatrix)) {
        return;
      }
      canvas->concat(invertMatrix);
      drawBackground(args, canvas);
    };
    auto backgroundPicture = CreatePicture(drawArgs, contentScale, backgroundDrawer);
    source->background = CreatePictureImage(backgroundPicture, &source->backgroundOffset);
  }
  return source;
}

void Layer::drawLayerStyles(Canvas* canvas, float alpha, const LayerStyleSource* source,
                            LayerStylePosition position) {
  DEBUG_ASSERT(source != nullptr && !FloatNearlyZero(source->contentScale));
  auto matrix = Matrix::MakeScale(1.f / source->contentScale, 1.f / source->contentScale);
  matrix.preTranslate(source->contentOffset.x, source->contentOffset.y);
  auto& contour = source->contour;
  auto contourOffset = source->contourOffset - source->contentOffset;
  auto& background = source->background;
  auto backgroundOffset = source->backgroundOffset - source->contentOffset;
  for (const auto& layerStyle : _layerStyles) {
    if (layerStyle->position() != position) {
      continue;
    }
    AutoCanvasRestore autoRestore(canvas);
    canvas->concat(matrix);
    switch (layerStyle->extraSourceType()) {
      case LayerStyleExtraSourceType::None:
        layerStyle->draw(canvas, source->content, source->contentScale, alpha);
        break;
      case LayerStyleExtraSourceType::Background:
        if (background != nullptr) {
          layerStyle->drawWithExtraSource(canvas, source->content, source->contentScale, background,
                                          backgroundOffset, alpha);
        }
        break;
      case LayerStyleExtraSourceType::Contour:
        if (contour != nullptr) {
          layerStyle->drawWithExtraSource(canvas, source->content, source->contentScale, contour,
                                          contourOffset, alpha);
        }
        break;
    }
  }
}

bool Layer::getLayersUnderPointInternal(float x, float y,
                                        std::vector<std::shared_ptr<Layer>>* results) {
  bool hasLayerUnderPoint = false;
  for (auto item = _children.rbegin(); item != _children.rend(); ++item) {
    const auto& childLayer = *item;
    if (!childLayer->visible()) {
      continue;
    }

    if (nullptr != childLayer->_scrollRect) {
      auto pointInChildSpace = childLayer->globalToLocal(Point::Make(x, y));
      if (!childLayer->_scrollRect->contains(pointInChildSpace.x, pointInChildSpace.y)) {
        continue;
      }
    }

    if (nullptr != childLayer->_mask) {
      if (!childLayer->_mask->hitTestPoint(x, y)) {
        continue;
      }
    }

    if (childLayer->getLayersUnderPointInternal(x, y, results)) {
      hasLayerUnderPoint = true;
    }
  }

  if (hasLayerUnderPoint) {
    results->push_back(weakThis.lock());
  } else {
    auto content = getContent();
    if (nullptr != content) {
      auto layerBoundsRect = content->getBounds();
      auto localPoint = globalToLocal(Point::Make(x, y));
      if (layerBoundsRect.contains(localPoint.x, localPoint.y)) {
        results->push_back(weakThis.lock());
        hasLayerUnderPoint = true;
      }
    }
  }

  return hasLayerUnderPoint;
}

bool Layer::hasValidMask() const {
  return _mask && _mask->root() == root();
}

}  // namespace tgfx
