/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "contents/LayerContent.h"
#include "contents/RasterizedContent.h"
#include "core/images/PictureImage.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/ContourContext.h"
#include "layers/DrawArgs.h"
#include "layers/RegionTransformer.h"
#include "layers/RootLayer.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/ShapeLayer.h"

namespace tgfx {
static std::atomic_bool AllowsEdgeAntialiasing = true;
static std::atomic_bool AllowsGroupOpacity = false;

struct LayerStyleSource {
  float contentScale = 1.0f;
  std::shared_ptr<Image> content = nullptr;
  Point contentOffset = {};
  std::shared_ptr<Image> contour = nullptr;
  Point contourOffset = {};
};

std::shared_ptr<Picture> Layer::RecordPicture(DrawMode mode, float contentScale,
                                              const std::function<void(Canvas*)>& drawFunction) {
  if (drawFunction == nullptr) {
    return nullptr;
  }
  if (mode == DrawMode::Contour) {
    ContourContext contourContext;
    auto contentCanvas = Canvas(&contourContext);
    contentCanvas.scale(contentScale, contentScale);
    drawFunction(&contentCanvas);
    return contourContext.finishRecordingAsPicture();
  }
  Recorder recorder = {};
  auto contentCanvas = recorder.beginRecording();
  contentCanvas->scale(contentScale, contentScale);
  drawFunction(contentCanvas);
  return recorder.finishRecordingAsPicture();
}

static std::shared_ptr<Image> ToImageWithOffset(
    std::shared_ptr<Picture> picture, Point* offset, const Rect* imageBounds = nullptr,
    std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB()) {
  if (picture == nullptr) {
    return nullptr;
  }
  auto bounds = imageBounds ? *imageBounds : picture->getBounds();
  bounds.roundOut();
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(std::move(picture), static_cast<int>(bounds.width()),
                               static_cast<int>(bounds.height()), &matrix, std::move(colorSpace));
  if (offset) {
    offset->x = bounds.left;
    offset->y = bounds.top;
  }
  return image;
}

static std::optional<Rect> GetClipBounds(const Canvas* canvas) {
  if (canvas == nullptr) {
    return std::nullopt;
  }
  const auto& clipPath = canvas->getTotalClip();
  auto clipRect = Rect::MakeEmpty();
  auto surface = canvas->getSurface();
  if (clipPath.isInverseFillType()) {
    if (!surface) {
      return std::nullopt;
    }
    clipRect = Rect::MakeWH(surface->width(), surface->height());
  } else {
    clipRect = clipPath.getBounds();
    if (surface && !clipRect.intersect(Rect::MakeWH(surface->width(), surface->height()))) {
      return Rect::MakeEmpty();
    }
  }
  if (clipRect.isEmpty()) {
    return Rect::MakeEmpty();
  }
  auto invert = Matrix::I();
  auto viewMatrix = canvas->getMatrix();
  if (!viewMatrix.invert(&invert)) {
    return Rect::MakeEmpty();
  }
  clipRect = invert.mapRect(clipRect);
  clipRect.roundOut();
  return clipRect;
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
  return std::shared_ptr<Layer>(new Layer());
}

Layer::~Layer() {
  for (const auto& filter : _filters) {
    filter->detachFromLayer(this);
  }
  if (_mask) {
    _mask->maskOwner = nullptr;
  }
  removeChildren();
  delete contentBounds;
}

Layer::Layer() {
  memset(&bitFields, 0, sizeof(bitFields));
  bitFields.visible = true;
  bitFields.allowsEdgeAntialiasing = AllowsEdgeAntialiasing;
  bitFields.allowsGroupOpacity = AllowsGroupOpacity;
  bitFields.blendMode = static_cast<uint8_t>(BlendMode::SrcOver);
  bitFields.passThroughBackground = true;
}

void Layer::setAlpha(float value) {
  if (_alpha == value) {
    return;
  }
  _alpha = value;
  invalidateTransform();
}

void Layer::setBlendMode(BlendMode value) {
  uint8_t uValue = static_cast<uint8_t>(value);
  if (bitFields.blendMode == uValue) {
    return;
  }
  bitFields.blendMode = uValue;
  invalidateTransform();
}

void Layer::setPassThroughBackground(bool value) {
  if (bitFields.passThroughBackground == value) {
    return;
  }
  bitFields.passThroughBackground = value;
  invalidateTransform();
}

void Layer::setPosition(const Point& value) {
  if (_matrix.getTranslateX() == value.x && _matrix.getTranslateY() == value.y) {
    return;
  }
  _matrix.setTranslateX(value.x);
  _matrix.setTranslateY(value.y);
  invalidateTransform();
}

void Layer::setMatrix(const Matrix& value) {
  if (_matrix == value) {
    return;
  }
  _matrix = value;
  invalidateTransform();
}

void Layer::setVisible(bool value) {
  if (bitFields.visible == value) {
    return;
  }
  bitFields.visible = value;
  invalidateTransform();
}

void Layer::setShouldRasterize(bool value) {
  if (bitFields.shouldRasterize == value) {
    return;
  }
  bitFields.shouldRasterize = value;
  invalidateTransform();
}

void Layer::setRasterizationScale(float value) {
  if (value < 0) {
    value = 0;
  }
  _rasterizationScale = value;
}

void Layer::setAllowsEdgeAntialiasing(bool value) {
  if (bitFields.allowsEdgeAntialiasing == value) {
    return;
  }
  bitFields.allowsEdgeAntialiasing = value;
  invalidateTransform();
}

void Layer::setAllowsGroupOpacity(bool value) {
  if (bitFields.allowsGroupOpacity == value) {
    return;
  }
  bitFields.allowsGroupOpacity = value;
  invalidateTransform();
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
  invalidateTransform();
}

void Layer::setMask(std::shared_ptr<Layer> value) {
  if (value.get() == this) {
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
  invalidateTransform();
}

void Layer::setMaskType(LayerMaskType value) {
  uint8_t uValue = static_cast<uint8_t>(value);
  if (bitFields.maskType == uValue) {
    return;
  }
  bitFields.maskType = uValue;
  if (_mask) {
    invalidateTransform();
  }
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
  invalidateTransform();
}

Layer* Layer::root() const {
  return _root;
}

void Layer::setLayerStyles(std::vector<std::shared_ptr<LayerStyle>> value) {
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
  invalidateTransform();
}

void Layer::setExcludeChildEffectsInLayerStyle(bool value) {
  if (value == bitFields.excludeChildEffectsInLayerStyle) {
    return;
  }
  bitFields.excludeChildEffectsInLayerStyle = value;
  invalidateTransform();
}

bool Layer::addChild(std::shared_ptr<Layer> child) {
  if (!child) {
    return false;
  }
  auto index = _children.size();
  if (child->_parent == this) {
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
  if (child->_parent == this) {
    return setChildIndex(child, index);
  }
  child->removeFromParent();
  _children.insert(_children.begin() + index, child);
  child->_parent = this;
  child->onAttachToRoot(_root);
  child->invalidateTransform();
  invalidateDescendents();
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
  if (_root) {
    _root->invalidateRect(child->renderBounds);
    child->renderBounds = {};
  }
  invalidateDescendents();
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
  if (_root) {
    // Immediately invalidate the old render bounds, as this may affect the background of the above
    // layer styles.
    _root->invalidateRect(child->renderBounds);
    child->renderBounds = {};
  }
  child->invalidateTransform();
  invalidateDescendents();
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
  invalidateDescendents();
  return true;
}

Rect Layer::getBounds(const Layer* targetCoordinateSpace, bool computeTightBounds) {
  auto matrix = getRelativeMatrix(targetCoordinateSpace);
  return getBoundsInternal(matrix, computeTightBounds);
}

Rect Layer::getBoundsInternal(const Matrix& coordinateMatrix, bool computeTightBounds) {
  Rect bounds = {};
  if (auto content = getContent()) {
    if (computeTightBounds) {
      bounds.join(content->getTightBounds(coordinateMatrix));
    } else {
      bounds.join(coordinateMatrix.mapRect(content->getBounds()));
    }
  }
  for (const auto& child : _children) {
    // Alpha does not need to be checked; alpha == 0 is still valid.
    if (!child->visible() || child->maskOwner) {
      continue;
    }
    auto childMatrix = child->getMatrixWithScrollRect();
    childMatrix.postConcat(coordinateMatrix);
    auto childBounds = child->getBoundsInternal(childMatrix, computeTightBounds);
    if (child->_scrollRect) {
      auto relatvieScrollRect = childMatrix.mapRect(*child->_scrollRect);
      if (!childBounds.intersect(relatvieScrollRect)) {
        continue;
      }
    }
    if (child->hasValidMask()) {
      auto maskRelativeMatrix = child->_mask->getRelativeMatrix(child.get());
      maskRelativeMatrix.postConcat(childMatrix);
      auto maskBounds = child->_mask->getBoundsInternal(maskRelativeMatrix, computeTightBounds);
      if (!childBounds.intersect(maskBounds)) {
        continue;
      }
    }
    bounds.join(childBounds);
  }

  if (!_layerStyles.empty() || !_filters.empty()) {
    auto contentScale = coordinateMatrix.getMaxScale();
    auto layerBounds = bounds;
    for (auto& layerStyle : _layerStyles) {
      auto styleBounds = layerStyle->filterBounds(layerBounds, contentScale);
      bounds.join(styleBounds);
    }
    for (auto& filter : _filters) {
      bounds = filter->filterBounds(bounds, contentScale);
    }
  }
  return bounds;
}

Point Layer::globalToLocal(const Point& globalPoint) const {
  auto globalMatrix = getGlobalMatrix();
  Matrix inverseMatrix = {};
  if (!globalMatrix.invert(&inverseMatrix)) {
    return Point::Make(0, 0);
  }
  return inverseMatrix.mapXY(globalPoint.x, globalPoint.y);
}

Point Layer::localToGlobal(const Point& localPoint) const {
  auto globalMatrix = getGlobalMatrix();
  return globalMatrix.mapXY(localPoint.x, localPoint.y);
}

bool Layer::hitTestPoint(float x, float y, bool shapeHitTest) {
  if (auto content = getContent()) {
    Point localPoint = globalToLocal(Point::Make(x, y));
    if (content->hitTestPoint(localPoint.x, localPoint.y, shapeHitTest)) {
      return true;
    }
  }

  for (const auto& childLayer : _children) {
    // Alpha does not need to be checked; alpha == 0 is still valid.
    if (!childLayer->visible() || childLayer->maskOwner) {
      continue;
    }

    if (nullptr != childLayer->_scrollRect) {
      auto pointInChildSpace = childLayer->globalToLocal(Point::Make(x, y));
      if (!childLayer->_scrollRect->contains(pointInChildSpace.x, pointInChildSpace.y)) {
        continue;
      }
    }

    if (nullptr != childLayer->_mask) {
      if (!childLayer->_mask->hitTestPoint(x, y, shapeHitTest)) {
        continue;
      }
    }

    if (childLayer->hitTestPoint(x, y, shapeHitTest)) {
      return true;
    }
  }

  return false;
}

static Rect GetClippedBounds(const Rect& bounds, const Canvas* canvas) {
  DEBUG_ASSERT(canvas != nullptr);
  auto clippedBounds = bounds;
  auto clipRect = GetClipBounds(canvas);
  if (!clipRect.has_value()) {
    return clippedBounds;
  }
  if (!clippedBounds.intersect(*clipRect)) {
    return Rect::MakeEmpty();
  }
  clippedBounds.roundOut();
  return clippedBounds;
}

void Layer::draw(Canvas* canvas, float alpha, BlendMode blendMode) {
  if (canvas == nullptr || alpha <= 0) {
    return;
  }

  auto surface = canvas->getSurface();
  DrawArgs args = {};
  Context* context = nullptr;

  auto bounds = getBounds();
  auto clippedBounds = GetClippedBounds(bounds, canvas);
  if (clippedBounds.isEmpty()) {
    return;
  }
  auto localToGlobalMatrix = getGlobalMatrix();
  auto globalToLocalMatrix = Matrix::I();
  bool canInvert = localToGlobalMatrix.invert(&globalToLocalMatrix);

  Rect renderRect = {};
  if (_root && canInvert) {
    _root->updateRenderBounds();
    renderRect = localToGlobalMatrix.mapRect(clippedBounds);
    args.renderRect = &renderRect;
  }

  if (surface) {
    args.dstColorSpace = surface->colorSpace();
    context = surface->getContext();
    if (!(surface->renderFlags() & RenderFlags::DisableCache)) {
      args.context = context;
    }
  } else if (bitFields.hasBlendMode) {
    auto scale = canvas->getMatrix().getMaxScale();
    args.blendModeBackground =
        BackgroundContext::Make(nullptr, Rect::MakeEmpty(), 0, 0, Matrix::MakeScale(scale, scale));
  }

  if (context && canInvert && hasBackgroundStyle()) {
    auto scale = canvas->getMatrix().getMaxScale();
    auto backgroundRect = clippedBounds;
    backgroundRect.scale(scale, scale);
    auto backgroundMatrix = globalToLocalMatrix;
    backgroundMatrix.postScale(scale, scale);
    if (auto backgroundContext = createBackgroundContext(context, backgroundRect, backgroundMatrix,
                                                         bounds == clippedBounds)) {
      auto backgroundCanvas = backgroundContext->getCanvas();
      auto actualMatrix = backgroundCanvas->getMatrix();
      // Since the background recorder starts from the current layer, we need to pre-concatenate
      // localToGlobalMatrix to the background canvas matrix to ensure the coordinate space is
      // correct.
      actualMatrix.preConcat(localToGlobalMatrix);
      backgroundCanvas->setMatrix(actualMatrix);
      Point offset = {};
      if (auto image = getBackgroundImage(args, scale, &offset)) {
        AutoCanvasRestore autoRestore(backgroundCanvas);
        actualMatrix.preScale(1.0f / scale, 1.0f / scale);
        backgroundCanvas->setMatrix(actualMatrix);
        backgroundCanvas->drawImage(image, offset.x, offset.y);
      }
      args.blurBackground = std::move(backgroundContext);
    }
  }
  drawLayer(args, canvas, alpha, blendMode);
}

void Layer::invalidateContent() {
  if (bitFields.dirtyContent) {
    return;
  }
  bitFields.dirtyContent = true;
  bitFields.dirtyContentBounds = true;
  invalidateDescendents();
}

void Layer::invalidateTransform() {
  if (bitFields.dirtyTransform) {
    return;
  }
  bitFields.dirtyTransform = true;
  invalidate();
}

void Layer::invalidateDescendents() {
  if (bitFields.dirtyDescendents) {
    return;
  }
  bitFields.dirtyDescendents = true;
  rasterizedContent = nullptr;
  cacheContent = nullptr;
  invalidate();
}

void Layer::invalidate() {
  if (_parent) {
    _parent->invalidateDescendents();
  }
  if (maskOwner) {
    maskOwner->invalidateTransform();
  }
}

void Layer::onUpdateContent(LayerRecorder*) {
}

void Layer::attachProperty(LayerProperty* property) {
  if (property) {
    property->attachToLayer(this);
  }
}

void Layer::detachProperty(LayerProperty* property) {
  if (property) {
    property->detachFromLayer(this);
  }
}

void Layer::onAttachToRoot(RootLayer* rootLayer) {
  _root = rootLayer;
  for (auto& child : _children) {
    child->onAttachToRoot(rootLayer);
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
  Matrix matrix = {};
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
  if (bitFields.dirtyContent) {
    LayerRecorder recorder = {};
    onUpdateContent(&recorder);
    layerContent = recorder.finishRecording();
    bitFields.dirtyContent = false;
  }
  return layerContent.get();
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

RasterizedContent* Layer::getRasterizedCache(const DrawArgs& args, const Matrix& renderMatrix) {
  auto scale = renderMatrix.getMaxScale();
  if (!bitFields.shouldRasterize || args.context == nullptr ||
      (args.drawMode == DrawMode::Background && hasBackgroundStyle()) ||
      args.drawMode == DrawMode::Contour || args.excludeEffects) {
    return nullptr;
  }
  auto contextID = args.context->uniqueID();
  auto content = rasterizedContent.get();
  float contentScale = _rasterizationScale;
  if (contentScale <= 0.0f) {
    contentScale = scale;
  }
  if (content && content->contextID() == contextID && content->contentScale() == contentScale) {
    return content;
  }
  Matrix drawingMatrix = {};
  auto offscreenArgs = args;
  offscreenArgs.context = nullptr;
  auto image = getRasterizedImage(offscreenArgs, contentScale, &drawingMatrix);
  if (image == nullptr) {
    return nullptr;
  }
  image = image->makeRasterized();
  if (image == nullptr) {
    return nullptr;
  }
  rasterizedContent =
      std::make_unique<RasterizedContent>(contextID, contentScale, std::move(image), drawingMatrix);
  return rasterizedContent.get();
}

std::shared_ptr<Image> Layer::getRasterizedImage(const DrawArgs& args, float contentScale,
                                                 Matrix* drawingMatrix) {
  DEBUG_ASSERT(drawingMatrix != nullptr);
  if (FloatNearlyZero(contentScale)) {
    return nullptr;
  }
  auto drawArgs = args;
  drawArgs.renderRect = nullptr;
  drawArgs.blurBackground = nullptr;
  auto picture = RecordPicture(drawArgs.drawMode, contentScale,
                               [&](Canvas* canvas) { drawDirectly(drawArgs, canvas, 1.0f); });
  if (!picture) {
    return nullptr;
  }
  Point offset = {};
  auto image = ToImageWithOffset(std::move(picture), &offset, nullptr, args.dstColorSpace);
  if (image == nullptr) {
    return nullptr;
  }
  auto filter = getImageFilter(contentScale);
  if (filter) {
    Point filterOffset = {};
    image = image->makeWithFilter(std::move(filter), &filterOffset);
    offset += filterOffset;
  }
  drawingMatrix->setScale(1.0f / contentScale, 1.0f / contentScale);
  drawingMatrix->preTranslate(offset.x, offset.y);
  return image;
}

void Layer::drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode) {
  DEBUG_ASSERT(canvas != nullptr);
  bool cache = false;
  if (args.renderRect) {
    auto scale = canvas->getMatrix().getMaxScale();
    auto scaleBounds = renderBounds;
    scaleBounds.scale(scale, scale);
    if (scaleBounds.width() < 1.f || scaleBounds.height() < 1.f) {
      return;
    }
    if (scaleBounds.width() <= 64.f && scaleBounds.height() <= 64.f && scale < 0.2f) {
      cache = !_layerStyles.empty();
    }
  }
  if (args.renderRect && !Rect::Intersects(*args.renderRect, renderBounds)) {
    if (args.blurBackground) {
      auto backgroundRect = args.blurBackground->getBackgroundRect();
      if (!Rect::Intersects(*args.renderRect, backgroundRect)) {
        return;
      }
      auto backgroundArgs = args;
      backgroundArgs.drawMode = DrawMode::Background;
      backgroundArgs.blurBackground = nullptr;
      backgroundArgs.renderRect = &backgroundRect;
      drawLayer(backgroundArgs, args.blurBackground->getCanvas(), alpha, blendMode);
    }
    return;
  }
  if (auto rasterizedCache = getRasterizedCache(args, canvas->getMatrix())) {
    rasterizedCache->draw(canvas, bitFields.allowsEdgeAntialiasing, alpha, blendMode);
    if (args.blurBackground) {
      if (hasBackgroundStyle()) {
        auto backgroundArgs = args;
        backgroundArgs.drawMode = DrawMode::Background;
        backgroundArgs.blurBackground = nullptr;
        drawOffscreen(backgroundArgs, args.blurBackground->getCanvas(), alpha, blendMode);
      } else {
        rasterizedCache->draw(args.blurBackground->getCanvas(), bitFields.allowsEdgeAntialiasing,
                              alpha, blendMode);
      }
    }
  } else if (cache || blendMode != BlendMode::SrcOver || !bitFields.passThroughBackground ||
             (alpha < 1.0f && bitFields.allowsGroupOpacity) || bitFields.shouldRasterize ||
             (!_filters.empty() && !args.excludeEffects) || hasValidMask()) {
    drawOffscreen(args, canvas, alpha, blendMode);
  } else {
    // draw directly
    drawDirectly(args, canvas, alpha);
  }
}

Matrix Layer::getRelativeMatrix(const Layer* targetCoordinateSpace) const {
  if (targetCoordinateSpace == nullptr || targetCoordinateSpace == this) {
    return {};
  }
  auto targetLayerMatrix = targetCoordinateSpace->getGlobalMatrix();
  Matrix targetLayerInverseMatrix = {};
  if (!targetLayerMatrix.invert(&targetLayerInverseMatrix)) {
    return {};
  }
  Matrix relativeMatrix = getGlobalMatrix();
  relativeMatrix.postConcat(targetLayerInverseMatrix);
  return relativeMatrix;
}

std::shared_ptr<MaskFilter> Layer::getMaskFilter(const DrawArgs& args, float scale,
                                                 const std::optional<Rect>& layerClipBounds) {
  auto maskArgs = args;
  auto maskType = static_cast<LayerMaskType>(bitFields.maskType);
  maskArgs.drawMode = maskType != LayerMaskType::Contour ? DrawMode::Normal : DrawMode::Contour;
  maskArgs.blurBackground = nullptr;
  std::shared_ptr<Picture> maskPicture = nullptr;
  auto relativeMatrix = _mask->getRelativeMatrix(this);
  auto maskClipBounds = layerClipBounds;
  if (layerClipBounds.has_value()) {
    auto invertedMatrix = Matrix::I();
    if (relativeMatrix.invert(&invertedMatrix)) {
      maskClipBounds = invertedMatrix.mapRect(*layerClipBounds);
    }
  }
  maskPicture = RecordPicture(maskArgs.drawMode, scale, [&](Canvas* canvas) {
    if (maskClipBounds.has_value()) {
      canvas->clipRect(*maskClipBounds);
    }
    _mask->drawLayer(maskArgs, canvas, _mask->_alpha, BlendMode::SrcOver);
  });
  if (maskPicture == nullptr) {
    return nullptr;
  }
  Point maskImageOffset = {};
  auto maskContentImage =
      ToImageWithOffset(std::move(maskPicture), &maskImageOffset, nullptr, args.dstColorSpace);
  if (maskContentImage == nullptr) {
    return nullptr;
  }
  if (maskType == LayerMaskType::Luminance) {
    maskContentImage =
        maskContentImage->makeWithFilter(ImageFilter::ColorFilter(ColorFilter::Luma()));
  }
  relativeMatrix.preScale(1.0f / scale, 1.0f / scale);
  relativeMatrix.preTranslate(maskImageOffset.x, maskImageOffset.y);
  relativeMatrix.postScale(scale, scale);

  auto shader = Shader::MakeImageShader(maskContentImage, TileMode::Decal, TileMode::Decal);
  if (shader) {
    shader = shader->makeWithMatrix(relativeMatrix);
  }
  return MaskFilter::MakeShader(shader);
}

std::shared_ptr<Image> Layer::getOffscreenContentImage(
    const DrawArgs& args, const Canvas* canvas, bool passThroughBackground,
    std::shared_ptr<BackgroundContext> subBackgroundContext, std::optional<Rect> clipBounds,
    Matrix* imageMatrix) {
  DEBUG_ASSERT(imageMatrix);
  auto inputBounds = getBounds();
  if (clipBounds.has_value()) {
    if (!inputBounds.intersect(*clipBounds)) {
      return nullptr;
    }
  }
  if (!args.excludeEffects) {
    // clipBounds is in local coordinate space,  so we getImageFilter with scale 1.0f.
    auto filter = getImageFilter(1.0f);
    if (filter) {
      inputBounds = filter->filterBounds(inputBounds, MapDirection::Reverse);
    }
  }

  auto offscreenArgs = args;
  auto contentScale = canvas->getMatrix().getMaxScale();
  offscreenArgs.blurBackground = std::move(subBackgroundContext);
  if (!canvas->getSurface() && passThroughBackground) {
    offscreenArgs.blendModeBackground =
        BackgroundContext::Make(nullptr, renderBounds, 0, 0, Matrix::MakeScale(contentScale));
  }

  std::shared_ptr<Image> finalImage = nullptr;
  auto context = canvas->getSurface() ? canvas->getSurface()->getContext() : nullptr;
  if (context && passThroughBackground) {
    auto currentMatrix = canvas->getMatrix();
    auto surfaceRect = currentMatrix.mapRect(inputBounds);
    surfaceRect.roundOut();
    auto offscreenSurface = Surface::Make(context, static_cast<int>(surfaceRect.width()),
                                          static_cast<int>(surfaceRect.height()), false, 1, false,
                                          0, args.dstColorSpace);
    auto offscreenCanvas = offscreenSurface->getCanvas();
    offscreenCanvas->translate(-surfaceRect.left, -surfaceRect.top);
    offscreenCanvas->drawImage(canvas->getSurface()->makeImageSnapshot());
    offscreenCanvas->clipPath(canvas->getTotalClip());
    offscreenCanvas->concat(currentMatrix);
    drawDirectly(offscreenArgs, offscreenCanvas, 1.0f);
    finalImage = offscreenSurface->makeImageSnapshot();
    offscreenCanvas->getMatrix().invert(imageMatrix);
  } else {
    Recorder recorder = {};
    auto offscreenCanvas = recorder.beginRecording();
    offscreenCanvas->scale(contentScale, contentScale);
    offscreenCanvas->clipRect(inputBounds);
    if (passThroughBackground && args.blendModeBackground) {
      Point offset = {};
      auto image = args.blendModeBackground->getBackgroundImage(&offset);
      offscreenCanvas->concat(args.blendModeBackground->backgroundMatrix());
      offscreenCanvas->drawImage(image, offset.x, offset.y);
    }
    drawDirectly(offscreenArgs, offscreenCanvas, 1.0f);
    Point offset;
    finalImage = ToImageWithOffset(recorder.finishRecordingAsPicture(), &offset, nullptr,
                                   args.dstColorSpace);
    imageMatrix->setScale(1.0f / contentScale, 1.0f / contentScale);
    imageMatrix->preTranslate(offset.x, offset.y);
  }
  return finalImage;
}
void Layer::drawOffscreen(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode) {
  auto contentScale = canvas->getMatrix().getMaxScale();
  if (FloatNearlyZero(contentScale)) {
    return;
  }

  auto scaledBounds = renderBounds;
  scaledBounds.scale(contentScale, contentScale);
  bool cache =
      scaledBounds.width() <= 64.f && scaledBounds.height() <= 64.f && contentScale <= 0.2f;

  if (cache && cacheContent && cacheContent->contentScale() >= contentScale) {
    cacheContent->draw(canvas, bitFields.allowsEdgeAntialiasing, alpha, blendMode);
    if (args.blurBackground) {
      if (hasBackgroundStyle()) {
        auto backgroundArgs = args;
        backgroundArgs.drawMode = DrawMode::Background;
        backgroundArgs.blurBackground = nullptr;
        drawOffscreen(backgroundArgs, args.blurBackground->getCanvas(), alpha, blendMode);
      } else {
        cacheContent->draw(args.blurBackground->getCanvas(), bitFields.allowsEdgeAntialiasing,
                           alpha, blendMode);
      }
    }
  }

  auto passThroughBackground = bitFields.passThroughBackground && blendMode == BlendMode::SrcOver &&
                               _filters.empty() && bitFields.hasBlendMode == true;

  std::shared_ptr<BackgroundContext> subBackgroundContext = nullptr;
  if (passThroughBackground) {
    subBackgroundContext = args.blurBackground;
  } else {
    subBackgroundContext = args.blurBackground && hasBackgroundStyle()
                               ? args.blurBackground->createSubContext()
                               : nullptr;
  }
  // canvas of background clip bounds will be more large than canvas clip bounds.
  auto clipBounds = GetClipBounds(args.blurBackground ? args.blurBackground->getCanvas() : canvas);
  auto imageMatrix = Matrix::I();
  auto image = getOffscreenContentImage(args, canvas, passThroughBackground, subBackgroundContext,
                                        clipBounds, &imageMatrix);
  auto invertImageMatrix = Matrix::I();
  if (image == nullptr || !imageMatrix.invert(&invertImageMatrix)) {
    return;
  }

  auto filterOffset = Point::Make(0, 0);
  if (!args.excludeEffects) {
    auto filter = getImageFilter(contentScale);
    if (filter) {
      if (clipBounds.has_value()) {
        clipBounds = invertImageMatrix.mapRect(*clipBounds);
        clipBounds->roundOut();
      }
      // clipBounds may be smaller than the image bounds, so we need to pass it to makeWithFilter.
      image = image->makeWithFilter(filter, &filterOffset,
                                    clipBounds.has_value() ? &*clipBounds : nullptr);
      imageMatrix.preTranslate(filterOffset.x, filterOffset.y);
      invertImageMatrix.postTranslate(-filterOffset.x, -filterOffset.y);
    }
  }
  if (image == nullptr) {
    return;
  }
  if (cache || (args.blurBackground && !subBackgroundContext)) {
    image = image->makeRasterized();
  }

  Paint paint = {};
  paint.setAntiAlias(bitFields.allowsEdgeAntialiasing);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);

  if (hasValidMask()) {
    auto maskFilter = getMaskFilter(args, contentScale, clipBounds);
    // if mask filter is nullptr while mask is valid, that means the layer is not visible.
    if (!maskFilter) {
      return;
    }
    auto maskMatrix = Matrix::MakeScale(1.0f / contentScale, 1.0f / contentScale);
    maskMatrix.postConcat(invertImageMatrix);
    maskFilter = maskFilter->makeWithMatrix(maskMatrix);
    paint.setMaskFilter(maskFilter);
  }

  AutoCanvasRestore autoRestore(canvas);
  canvas->concat(imageMatrix);
  canvas->drawImage(image, &paint);
  if (args.blendModeBackground) {
    auto blendCanvas = args.blendModeBackground->getCanvas();
    AutoCanvasRestore autoRestoreBlend(blendCanvas);
    blendCanvas->concat(imageMatrix);
    blendCanvas->drawImage(image, &paint);
  }
  if (subBackgroundContext) {
    subBackgroundContext->drawToParent(Matrix::MakeScale(1.0f / contentScale), paint);
  } else if (args.blurBackground && subBackgroundContext != args.blurBackground) {
    auto backgroundCanvas = args.blurBackground->getCanvas();
    AutoCanvasRestore autoRestoreBg(backgroundCanvas);
    backgroundCanvas->concat(imageMatrix);
    backgroundCanvas->drawImage(image, &paint);
  }

  if (cache) {
    cacheContent = std::make_unique<RasterizedContent>(args.context->uniqueID(), contentScale,
                                                       std::move(image), imageMatrix);
  }
}

void Layer::drawDirectly(const DrawArgs& args, Canvas* canvas, float alpha) {
  auto layerStyleSource = getLayerStyleSource(args, canvas->getMatrix());
  drawContents(args, canvas, alpha, layerStyleSource.get());
}

class LayerFill : public FillModifier {
 public:
  LayerFill(bool antiAlias, float alpha) : antiAlias(antiAlias), alpha(alpha) {
  }

  Fill transform(const Fill& fill) const override {
    auto newFill = fill;
    newFill.color.alpha *= alpha;
    newFill.antiAlias = antiAlias;
    return newFill;
  }

 private:
  bool antiAlias = true;
  float alpha = 1.0f;
};

void Layer::drawContents(const DrawArgs& args, Canvas* canvas, float alpha,
                         const LayerStyleSource* layerStyleSource, const Layer* stopChild) {
  if (layerStyleSource) {
    drawLayerStyles(args, canvas, alpha, layerStyleSource, LayerStylePosition::Below);
  }
  LayerFill layerFill(bitFields.allowsEdgeAntialiasing, alpha);
  auto content = getContent();
  if (content) {
    if (args.drawMode == DrawMode::Contour) {
      content->drawContour(canvas, &layerFill);
    } else {
      content->drawDefault(canvas, &layerFill);
      if (args.blurBackground) {
        content->drawDefault(args.blurBackground->getCanvas(), &layerFill);
      }
      if (args.blendModeBackground) {
        content->drawDefault(args.blendModeBackground->getCanvas(), &layerFill);
      }
    }
  }
  if (!drawChildren(args, canvas, alpha, stopChild)) {
    return;
  }
  if (layerStyleSource) {
    drawLayerStyles(args, canvas, alpha, layerStyleSource, LayerStylePosition::Above);
  }
  if (content && args.drawMode != DrawMode::Contour) {
    content->drawForeground(canvas, &layerFill);
    if (args.blurBackground) {
      content->drawForeground(args.blurBackground->getCanvas(), &layerFill);
    }
    if (args.blendModeBackground) {
      content->drawForeground(args.blendModeBackground->getCanvas(), &layerFill);
    }
  }
}

bool Layer::drawChildren(const DrawArgs& args, Canvas* canvas, float alpha,
                         const Layer* stopChild) {
  int lastBackgroundLayerIndex = -1;
  if (args.forceDrawBackground) {
    lastBackgroundLayerIndex = static_cast<int>(_children.size()) - 1;
  } else if (hasBackgroundStyle()) {
    for (int i = static_cast<int>(_children.size()) - 1; i >= 0; --i) {
      if (_children[static_cast<size_t>(i)]->hasBackgroundStyle()) {
        lastBackgroundLayerIndex = i;
        break;
      }
    }
  }
  for (size_t i = 0; i < _children.size(); ++i) {
    auto& child = _children[i];
    if (child.get() == stopChild) {
      return false;
    }
    if (child->maskOwner) {
      continue;
    }
    if (!child->visible() || child->_alpha <= 0) {
      continue;
    }
    auto childArgs = args;
    if (static_cast<int>(i) < lastBackgroundLayerIndex) {
      childArgs.forceDrawBackground = true;
    } else {
      childArgs.forceDrawBackground = false;
      if (static_cast<int>(i) > lastBackgroundLayerIndex) {
        childArgs.blurBackground = nullptr;
      }
    }

    AutoCanvasRestore autoRestore(canvas);
    auto backgroundCanvas =
        childArgs.blurBackground ? childArgs.blurBackground->getCanvas() : nullptr;
    auto blendCanvas = args.blendModeBackground ? args.blendModeBackground->getCanvas() : nullptr;
    canvas->concat(child->getMatrixWithScrollRect());
    if (child->_scrollRect) {
      canvas->clipRect(*child->_scrollRect);
    }
    if (backgroundCanvas) {
      backgroundCanvas->save();
      backgroundCanvas->concat(child->getMatrixWithScrollRect());
      if (child->_scrollRect) {
        backgroundCanvas->clipRect(*child->_scrollRect);
      }
    }
    if (blendCanvas) {
      blendCanvas->save();
      blendCanvas->concat(child->getMatrixWithScrollRect());
      if (child->_scrollRect) {
        blendCanvas->clipRect(*child->_scrollRect);
      }
    }
    child->drawLayer(childArgs, canvas, child->_alpha * alpha,
                     static_cast<BlendMode>(child->bitFields.blendMode));
    if (backgroundCanvas) {
      backgroundCanvas->restore();
    }

    if (blendCanvas) {
      blendCanvas->restore();
    }
  }
  return true;
}

float Layer::drawBackgroundLayers(const DrawArgs& args, Canvas* canvas) {
  if (!_parent) {
    return _alpha;
  }
  // parent background -> parent layer styles (below) -> parent content -> sibling layers content
  auto currentAlpha = _parent->drawBackgroundLayers(args, canvas);
  auto layerStyleSource = _parent->getLayerStyleSource(args, canvas->getMatrix());
  _parent->drawContents(args, canvas, currentAlpha, layerStyleSource.get(), this);
  canvas->concat(getMatrixWithScrollRect());
  if (_scrollRect) {
    canvas->clipRect(*_scrollRect);
  }
  return currentAlpha * _alpha;
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

  DrawArgs drawArgs = args;
  drawArgs.blurBackground = nullptr;
  drawArgs.excludeEffects = bitFields.excludeChildEffectsInLayerStyle;
  // Use Mode::Contour to record the contour of the content, to prevent the subsequent use of
  // AlphaThresholdFilter from turning semi-transparent pixels into opaque pixels, which would cause
  // severe aliasing.
  auto contentPicture = RecordPicture(DrawMode::Contour, contentScale, [&](Canvas* canvas) {
    drawContents(drawArgs, canvas, 1.0f);
  });
  Point contentOffset = {};
  auto content =
      ToImageWithOffset(std::move(contentPicture), &contentOffset, nullptr, args.dstColorSpace);
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
    auto contourPicture = RecordPicture(DrawMode::Contour, contentScale, [&](Canvas* canvas) {
      drawContents(drawArgs, canvas, 1.0f);
    });
    source->contour = ToImageWithOffset(std::move(contourPicture), &source->contourOffset, nullptr,
                                        drawArgs.dstColorSpace);
  }
  return source;
}

std::shared_ptr<Image> Layer::getBackgroundImage(const DrawArgs& args, float contentScale,
                                                 Point* offset) {
  if (args.drawMode == DrawMode::Background) {
    return nullptr;
  }
  Recorder recorder = {};
  auto canvas = recorder.beginRecording();
  auto bounds = getBounds();
  bounds.scale(contentScale, contentScale);
  bounds.roundOut();
  canvas->clipRect(bounds);
  canvas->scale(contentScale, contentScale);
  auto globalMatrix = getGlobalMatrix();
  Matrix invertMatrix = {};
  if (!globalMatrix.invert(&invertMatrix)) {
    return nullptr;
  }
  canvas->concat(invertMatrix);
  if (args.blurBackground) {
    Point bgOffset = {};
    auto image = args.blurBackground->getBackgroundImage(offset);
    canvas->concat(args.blurBackground->backgroundMatrix());
    canvas->drawImage(image, bgOffset.x, bgOffset.y);
  } else {
    auto drawArgs = args;
    drawArgs.excludeEffects = false;
    // Set the draw mode to Background to avoid drawing the layer styles that require background.
    drawArgs.drawMode = DrawMode::Background;
    auto backgroundRect = renderBounds;
    if (drawArgs.renderRect) {
      backgroundRect.intersect(*args.renderRect);
      drawArgs.renderRect = &backgroundRect;
    }
    auto currentAlpha = drawBackgroundLayers(drawArgs, canvas);
    // Draw the layer styles below the content, as they are part of the background.
    auto layerStyleSource = getLayerStyleSource(drawArgs, canvas->getMatrix());
    if (layerStyleSource) {
      drawLayerStyles(drawArgs, canvas, currentAlpha, layerStyleSource.get(),
                      LayerStylePosition::Below);
    }
  }
  auto backgroundPicture = recorder.finishRecordingAsPicture();
  return ToImageWithOffset(std::move(backgroundPicture), offset, &bounds, args.dstColorSpace);
}

void Layer::drawLayerStyles(const DrawArgs& args, Canvas* canvas, float alpha,
                            const LayerStyleSource* source, LayerStylePosition position) {
  DEBUG_ASSERT(source != nullptr && !FloatNearlyZero(source->contentScale));
  auto& contour = source->contour;
  auto contourOffset = source->contourOffset - source->contentOffset;
  auto backgroundCanvas = args.blurBackground ? args.blurBackground->getCanvas() : nullptr;
  auto blendModeCanvas = args.blendModeBackground ? args.blendModeBackground->getCanvas() : nullptr;
  auto matrix = Matrix::MakeScale(1.f / source->contentScale, 1.f / source->contentScale);
  matrix.preTranslate(source->contentOffset.x, source->contentOffset.y);
  AutoCanvasRestore autoRestore(canvas);
  canvas->concat(matrix);
  if (backgroundCanvas) {
    backgroundCanvas->save();
    backgroundCanvas->concat(matrix);
  }
  if (blendModeCanvas) {
    blendModeCanvas->save();
    blendModeCanvas->concat(matrix);
  }
  auto clipBounds =
      args.blurBackground ? GetClipBounds(args.blurBackground->getCanvas()) : std::nullopt;
  for (const auto& layerStyle : _layerStyles) {
    if (layerStyle->position() != position) {
      continue;
    }
    Recorder recorder = {};
    auto pictureCanvas = recorder.beginRecording();
    if (clipBounds.has_value()) {
      pictureCanvas->clipRect(*clipBounds);
    }
    switch (layerStyle->extraSourceType()) {
      case LayerStyleExtraSourceType::None:
        layerStyle->draw(pictureCanvas, source->content, source->contentScale, alpha);
        break;
      case LayerStyleExtraSourceType::Background: {
        Point backgroundOffset = {};
        auto background = getBackgroundImage(args, source->contentScale, &backgroundOffset);
        if (background != nullptr) {
          backgroundOffset = backgroundOffset - source->contentOffset;
          layerStyle->drawWithExtraSource(pictureCanvas, source->content, source->contentScale,
                                          background, backgroundOffset, alpha);
        }
        break;
      }
      case LayerStyleExtraSourceType::Contour:
        if (contour != nullptr) {
          layerStyle->drawWithExtraSource(pictureCanvas, source->content, source->contentScale,
                                          contour, contourOffset, alpha);
        }
        break;
    }
    auto picture = recorder.finishRecordingAsPicture();
    if (picture == nullptr) {
      continue;
    }
    if (!backgroundCanvas ||
        layerStyle->extraSourceType() == LayerStyleExtraSourceType::Background) {
      canvas->drawPicture(picture);
      if (blendModeCanvas) {
        blendModeCanvas->drawPicture(picture);
      }
    } else if (!clipBounds.has_value()) {
      canvas->drawPicture(picture);
      backgroundCanvas->drawPicture(picture);
      if (blendModeCanvas) {
        blendModeCanvas->drawPicture(picture);
      }
    } else {
      Point offset = {};
      auto image = ToImageWithOffset(std::move(picture), &offset, nullptr, args.dstColorSpace);
      if (image == nullptr) {
        continue;
      }
      image = image->makeRasterized();
      Paint paint = {};
      paint.setBlendMode(layerStyle->blendMode());
      canvas->drawImage(image, offset.x, offset.y, &paint);
      backgroundCanvas->drawImage(image, offset.x, offset.y, &paint);
      if (blendModeCanvas) {
        blendModeCanvas->drawImage(image, offset.x, offset.y, &paint);
      }
    }
  }
  if (backgroundCanvas) {
    backgroundCanvas->restore();
  }
  if (blendModeCanvas) {
    blendModeCanvas->restore();
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
    results->push_back(shared_from_this());
  } else {
    auto content = getContent();
    if (nullptr != content) {
      auto layerBoundsRect = content->getBounds();
      auto localPoint = globalToLocal(Point::Make(x, y));
      if (layerBoundsRect.contains(localPoint.x, localPoint.y)) {
        results->push_back(shared_from_this());
        hasLayerUnderPoint = true;
      }
    }
  }

  return hasLayerUnderPoint;
}

bool Layer::hasValidMask() const {
  return _mask && _mask->root() == root() && _mask->bitFields.visible;
}

void Layer::updateRenderBounds(std::shared_ptr<RegionTransformer> transformer, bool forceDirty) {
  if (!forceDirty && !bitFields.dirtyDescendents) {
    if (maxBackgroundOutset > 0 || bitFields.hasBlendMode) {
      propagateLayerState();
      if (_root->hasDirtyRegions()) {
        checkBackgroundStyles(transformer);
      }
    }
    return;
  }
  maxBackgroundOutset = 0;
  minBackgroundOutset = std::numeric_limits<float>::max();
  auto contentScale = 1.0f;
  if (!_layerStyles.empty() || !_filters.empty()) {
    if (transformer) {
      contentScale = transformer->getMaxScale();
    }
    transformer =
        RegionTransformer::MakeFromFilters(_filters, contentScale, std::move(transformer));
    transformer =
        RegionTransformer::MakeFromStyles(_layerStyles, contentScale, std::move(transformer));
  }
  auto content = getContent();
  if (bitFields.dirtyContentBounds || (forceDirty && content)) {
    if (contentBounds) {
      _root->invalidateRect(*contentBounds);
    } else {
      contentBounds = new Rect();
    }
    if (content) {
      *contentBounds = content->getBounds();
      if (transformer) {
        transformer->transform(contentBounds);
      }
      _root->invalidateRect(*contentBounds);
    } else {
      contentBounds->setEmpty();
    }
    bitFields.dirtyContentBounds = false;
  }
  if (contentBounds) {
    renderBounds = *contentBounds;
  } else {
    renderBounds.setEmpty();
  }
  for (auto& child : _children) {
    if (!child->bitFields.visible || child->_alpha <= 0) {
      if (child->bitFields.dirtyTransform) {
        _root->invalidateRect(child->renderBounds);
        child->renderBounds = {};
      }
      child->bitFields.dirtyTransform = false;
      continue;
    }
    auto childMatrix = child->getMatrixWithScrollRect();
    auto childTransformer = RegionTransformer::MakeFromMatrix(childMatrix, transformer);
    std::optional<Rect> clipRect = std::nullopt;
    if (child->_scrollRect) {
      clipRect = *child->_scrollRect;
    }
    if (child->hasValidMask()) {
      auto maskBounds = child->_mask->getBounds(child.get());
      if (clipRect.has_value()) {
        if (!clipRect->intersect(maskBounds)) {
          if (child->bitFields.dirtyTransform) {
            _root->invalidateRect(child->renderBounds);
            child->renderBounds = {};
          }
          child->bitFields.dirtyTransform = false;
          continue;
        }
      } else {
        clipRect = maskBounds;
      }
    }
    if (clipRect.has_value()) {
      childTransformer = RegionTransformer::MakeFromClip(*clipRect, std::move(childTransformer));
    }
    auto childForceDirty = forceDirty || child->bitFields.dirtyTransform;
    child->updateRenderBounds(childTransformer, childForceDirty);
    child->bitFields.dirtyTransform = false;
    if (!child->maskOwner) {
      renderBounds.join(child->renderBounds);
    }
  }
  auto backOutset = 0.f;
  for (auto& style : _layerStyles) {
    if (style->extraSourceType() != LayerStyleExtraSourceType::Background) {
      continue;
    }
    auto outset = style->filterBackground(Rect::MakeEmpty(), contentScale);
    backOutset = std::max(backOutset, outset.right);
    backOutset = std::max(backOutset, outset.bottom);
  }
  if (backOutset > 0) {
    maxBackgroundOutset = std::max(backOutset, maxBackgroundOutset);
    minBackgroundOutset = std::min(backOutset, minBackgroundOutset);
    updateBackgroundBounds(contentScale);
  }
  if (bitFields.blendMode != static_cast<uint8_t>(BlendMode::SrcOver)) {
    bitFields.hasBlendMode = true;
  }
  propagateLayerState();
  bitFields.dirtyDescendents = false;
}

void Layer::checkBackgroundStyles(std::shared_ptr<RegionTransformer> transformer) {
  for (auto& child : _children) {
    if (child->maxBackgroundOutset <= 0 || !child->bitFields.visible || child->_alpha <= 0) {
      continue;
    }
    auto childMatrix = child->getMatrixWithScrollRect();
    auto childTransformer = RegionTransformer::MakeFromMatrix(childMatrix, transformer);
    child->checkBackgroundStyles(childTransformer);
  }
  updateBackgroundBounds(transformer ? transformer->getMaxScale() : 1.0f);
}

void Layer::updateBackgroundBounds(float contentScale) {
  auto backgroundChanged = false;
  for (auto& style : _layerStyles) {
    if (style->extraSourceType() != LayerStyleExtraSourceType::Background) {
      continue;
    }
    if (_root->invalidateBackground(renderBounds, style.get(), contentScale)) {
      backgroundChanged = true;
    }
  }
  if (!backgroundChanged && bitFields.hasBlendMode) {
    if (_root->invalidateBackground(renderBounds, nullptr, contentScale)) {
      backgroundChanged = true;
    }
  }
  if (backgroundChanged) {
    auto layer = this;
    while (layer && !layer->bitFields.dirtyDescendents) {
      layer->rasterizedContent = nullptr;
      layer->cacheContent = nullptr;
      if (layer->maskOwner) {
        break;
      }
      layer = layer->_parent;
    }
  }
}

void Layer::propagateLayerState() {
  auto layer = _parent;
  while (layer) {
    bool change = false;
    if (layer->maxBackgroundOutset < maxBackgroundOutset) {
      layer->maxBackgroundOutset = maxBackgroundOutset;
      change = true;
    }
    if (layer->minBackgroundOutset > minBackgroundOutset) {
      layer->minBackgroundOutset = minBackgroundOutset;
      change = true;
    }
    // Only propagate hasBlendMode if this layer actually has a blend mode
    if (bitFields.hasBlendMode && !layer->bitFields.hasBlendMode) {
      layer->bitFields.hasBlendMode = true;
      change = true;
    }
    if (!change) {
      break;
    }
    layer = layer->_parent;
  }
}

bool Layer::hasBackgroundStyle() {
  if (!bitFields.dirtyDescendents && maxBackgroundOutset > 0) {
    return true;
  }
  for (const auto& style : _layerStyles) {
    if (style->extraSourceType() == LayerStyleExtraSourceType::Background) {
      return true;
    }
  }
  for (const auto& child : _children) {
    if (child->hasBackgroundStyle()) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<BackgroundContext> Layer::createBackgroundContext(Context* context,
                                                                  const Rect& drawRect,
                                                                  const Matrix& viewMatrix,
                                                                  bool fullLayer) const {
  if (maxBackgroundOutset <= 0.0f) {
    return nullptr;
  }
  if (fullLayer) {
    return BackgroundContext::Make(context, drawRect, 0, 0, viewMatrix);
  }
  auto scale = viewMatrix.getMaxScale();
  return BackgroundContext::Make(context, drawRect, maxBackgroundOutset * scale,
                                 minBackgroundOutset * scale, viewMatrix);
}

}  // namespace tgfx
