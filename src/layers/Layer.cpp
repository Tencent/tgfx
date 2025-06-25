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
#include "contents/LayerContent.h"
#include "contents/RasterizedContent.h"
#include "core/images/PictureImage.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
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

static std::shared_ptr<Picture> RecordPicture(float contentScale,
                                              const std::function<void(Canvas*)>& drawFunction) {
  if (drawFunction == nullptr) {
    return nullptr;
  }
  Recorder recorder = {};
  auto contentCanvas = recorder.beginRecording();
  contentCanvas->scale(contentScale, contentScale);
  drawFunction(contentCanvas);
  return recorder.finishRecordingAsPicture();
}

static std::shared_ptr<Image> ToImageWithOffset(std::shared_ptr<Picture> picture, Point* offset,
                                                Rect* imageBounds = nullptr) {
  if (picture == nullptr) {
    return nullptr;
  }
  auto bounds = imageBounds ? *imageBounds : picture->getBounds();
  bounds.roundOut();
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(std::move(picture), static_cast<int>(bounds.width()),
                               static_cast<int>(bounds.height()), &matrix);
  if (offset) {
    offset->x = bounds.left;
    offset->y = bounds.top;
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
  if (_rasterizationScale < 0) {
    _rasterizationScale = 0;
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
  invalidateTransform();
}

void Layer::setMaskType(LayerMaskType value) {
  if (_maskType == value) {
    return;
  }
  _maskType = value;
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

void Layer::draw(Canvas* canvas, float alpha, BlendMode blendMode) {
  if (canvas == nullptr || alpha <= 0) {
    return;
  }
  auto surface = canvas->getSurface();
  DrawArgs args = {};
  Context* context = nullptr;
  if (surface) {
    context = surface->getContext();
    if (!(surface->renderFlags() & RenderFlags::DisableCache)) {
      args.context = context;
    }
  }

  if (context && hasDescendantBackgroundStyle()) {
    auto scale = canvas->getMatrix().getMaxScale();
    auto bounds = getBounds();
    bounds.scale(scale, scale);
    auto globalMatrix = getGlobalMatrix();
    globalMatrix.preScale(1 / scale, 1 / scale);
    auto invert = Matrix::I();
    if (globalMatrix.invert(&invert)) {
      auto backgroundContext = BackgroundContext::Make(context, bounds, invert);
      if (backgroundContext) {
        auto backgroundCanvas = backgroundContext->getCanvas();
        auto image = getBackgroundImage(args, scale, nullptr);
        if (image) {
          AutoCanvasRestore restore(backgroundCanvas);
          backgroundCanvas->setMatrix(Matrix::I());
          backgroundCanvas->drawImage(image);
        }
        args.backgroundContext = std::move(backgroundContext);
      }
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
  if (!bitFields.shouldRasterize || args.context == nullptr ||
      (args.drawMode == DrawMode::Background && bitFields.hasBackgroundStyle) ||
      args.drawMode == DrawMode::Contour || args.excludeEffects) {
    return nullptr;
  }
  auto contextID = args.context->uniqueID();
  auto content = rasterizedContent.get();
  float contentScale =
      _rasterizationScale == 0.0f ? renderMatrix.getMaxScale() : _rasterizationScale;
  if (content && content->contextID() == contextID && content->contentScale() == contentScale) {
    return content;
  }
  Matrix drawingMatrix = {};
  auto image = getRasterizedImage(args, contentScale, &drawingMatrix);
  if (image == nullptr) {
    return nullptr;
  }
  image = image->makeTextureImage(args.context);
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
  drawArgs.backgroundContext = nullptr;
  auto picture =
      RecordPicture(contentScale, [&](Canvas* canvas) { drawDirectly(drawArgs, canvas, 1.0f); });
  if (!picture) {
    return nullptr;
  }
  Point offset = {};
  auto image = ToImageWithOffset(std::move(picture), &offset);
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
  if (args.renderRect && !args.renderRect->intersects(renderBounds)) {
    return;
  }
  if (auto rasterizedCache = getRasterizedCache(args, canvas->getMatrix())) {
    rasterizedCache->draw(canvas, bitFields.allowsEdgeAntialiasing, alpha, blendMode);
    if (args.backgroundContext) {
      if (bitFields.hasBackgroundStyle) {
        auto backgroundArgs = args;
        backgroundArgs.drawMode = DrawMode::Background;
        backgroundArgs.backgroundContext = nullptr;
        drawOffscreen(backgroundArgs, args.backgroundContext->getCanvas(), alpha, blendMode);
      } else {
        rasterizedCache->draw(args.backgroundContext->getCanvas(), bitFields.allowsEdgeAntialiasing,
                              alpha, blendMode);
      }
    }
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

std::shared_ptr<MaskFilter> Layer::getMaskFilter(const DrawArgs& args, float scale) {
  auto maskArgs = args;
  maskArgs.drawMode = _maskType != LayerMaskType::Contour ? DrawMode::Normal : DrawMode::Contour;
  maskArgs.backgroundContext = nullptr;
  auto maskPicture = RecordPicture(scale, [&](Canvas* canvas) {
    _mask->drawLayer(maskArgs, canvas, _mask->_alpha, BlendMode::SrcOver);
  });
  if (maskPicture == nullptr) {
    return nullptr;
  }
  Point maskImageOffset = {};
  auto maskContentImage = ToImageWithOffset(std::move(maskPicture), &maskImageOffset);
  if (maskContentImage == nullptr) {
    return nullptr;
  }
  if (_maskType == LayerMaskType::Luminance) {
    maskContentImage =
        maskContentImage->makeWithFilter(ImageFilter::ColorFilter(ColorFilter::Luma()));
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

  Paint paint = {};
  paint.setAntiAlias(bitFields.allowsEdgeAntialiasing);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  if (hasValidMask()) {
    auto maskFilter = getMaskFilter(args, contentScale);
    // if mask filter is nullptr while mask is valid, that means the layer is not visible.
    if (!maskFilter) {
      return;
    }
    paint.setMaskFilter(maskFilter);
  }
  auto subBackgroundContext =
      args.backgroundContext && (bitFields.hasBackgroundStyle || hasDescendantBackgroundStyle())
          ? args.backgroundContext->createSubContext()
          : nullptr;
  auto offscreenArgs = args;
  offscreenArgs.backgroundContext = subBackgroundContext;
  auto picture = RecordPicture(contentScale,
                               [&](Canvas* canvas) { drawDirectly(offscreenArgs, canvas, 1.0f); });
  if (picture == nullptr) {
    return;
  }
  if (!args.excludeEffects) {
    paint.setImageFilter(getImageFilter(contentScale));
  }
  auto matrix = Matrix::MakeScale(1.0f / contentScale);
  canvas->drawPicture(picture, &matrix, &paint);
  if (args.backgroundContext) {
    if (subBackgroundContext) {
      subBackgroundContext->drawToParent(matrix, paint);
    } else {
      args.backgroundContext->getCanvas()->drawPicture(std::move(picture), &matrix, &paint);
    }
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
      if (args.backgroundContext) {
        content->drawDefault(args.backgroundContext->getCanvas(), &layerFill);
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
    if (args.backgroundContext) {
      content->drawForeground(args.backgroundContext->getCanvas(), &layerFill);
    }
  }
}

bool Layer::drawChildren(const DrawArgs& args, Canvas* canvas, float alpha,
                         const Layer* stopChild) {
  for (const auto& child : _children) {
    if (child.get() == stopChild) {
      return false;
    }
    if (child->maskOwner) {
      continue;
    }
    if (!child->visible() || child->_alpha <= 0) {
      continue;
    }

    AutoCanvasRestore autoRestore(canvas);
    auto backgroundCanvas = args.backgroundContext ? args.backgroundContext->getCanvas() : nullptr;
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
    child->drawLayer(args, canvas, child->_alpha * alpha,
                     static_cast<BlendMode>(child->bitFields.blendMode));
    if (backgroundCanvas) {
      backgroundCanvas->restore();
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
  drawArgs.backgroundContext = nullptr;
  drawArgs.excludeEffects = bitFields.excludeChildEffectsInLayerStyle;
  auto contentPicture =
      RecordPicture(contentScale, [&](Canvas* canvas) { drawContents(drawArgs, canvas, 1.0f); });
  Point contentOffset = {};
  auto content = ToImageWithOffset(std::move(contentPicture), &contentOffset);
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
    auto contourPicture =
        RecordPicture(contentScale, [&](Canvas* canvas) { drawContents(drawArgs, canvas, 1.0f); });
    source->contour = ToImageWithOffset(std::move(contourPicture), &source->contourOffset);
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
  canvas->clipRect(bounds);
  canvas->scale(contentScale, contentScale);
  auto globalMatrix = getGlobalMatrix();
  Matrix invertMatrix = {};
  if (!globalMatrix.invert(&invertMatrix)) {
    return nullptr;
  }
  canvas->concat(invertMatrix);
  if (args.backgroundContext) {
    canvas->concat(args.backgroundContext->backgroundMatrix());
    canvas->drawImage(args.backgroundContext->getBackgroundImage());
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
  return ToImageWithOffset(std::move(backgroundPicture), offset, &bounds);
}

void Layer::drawLayerStyles(const DrawArgs& args, Canvas* canvas, float alpha,
                            const LayerStyleSource* source, LayerStylePosition position) {
  DEBUG_ASSERT(source != nullptr && !FloatNearlyZero(source->contentScale));
  auto matrix = Matrix::MakeScale(1.f / source->contentScale, 1.f / source->contentScale);
  matrix.preTranslate(source->contentOffset.x, source->contentOffset.y);
  auto& contour = source->contour;
  auto contourOffset = source->contourOffset - source->contentOffset;
  auto backgroundCanvas = args.backgroundContext ? args.backgroundContext->getCanvas() : nullptr;
  for (const auto& layerStyle : _layerStyles) {
    if (layerStyle->position() != position) {
      continue;
    }
    AutoCanvasRestore autoRestore(canvas);
    canvas->concat(matrix);
    if (backgroundCanvas) {
      backgroundCanvas->save();
      backgroundCanvas->concat(matrix);
    }
    switch (layerStyle->extraSourceType()) {
      case LayerStyleExtraSourceType::None:
        layerStyle->draw(canvas, source->content, source->contentScale, alpha);
        if (backgroundCanvas) {
          layerStyle->draw(backgroundCanvas, source->content, source->contentScale, alpha);
        }
        break;
      case LayerStyleExtraSourceType::Background: {
        Point backgroundOffset = {};
        auto background = getBackgroundImage(args, source->contentScale, &backgroundOffset);
        if (background != nullptr) {
          backgroundOffset = backgroundOffset - source->contentOffset;
          layerStyle->drawWithExtraSource(canvas, source->content, source->contentScale, background,
                                          backgroundOffset, alpha);
        }
        break;
      }
      case LayerStyleExtraSourceType::Contour:
        if (contour != nullptr) {
          layerStyle->drawWithExtraSource(canvas, source->content, source->contentScale, contour,
                                          contourOffset, alpha);
          if (backgroundCanvas) {
            layerStyle->drawWithExtraSource(backgroundCanvas, source->content, source->contentScale,
                                            contour, contourOffset, alpha);
          }
        }
        break;
    }
    if (backgroundCanvas) {
      backgroundCanvas->restore();
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

void Layer::updateRenderBounds(const Matrix& renderMatrix,
                               std::shared_ptr<RegionTransformer> transformer, bool forceDirty) {
  if (!forceDirty && !bitFields.dirtyDescendents) {
    if (bitFields.hasBackgroundStyle) {
      propagateHasBackgroundStyleFlags();
      if (_root->hasDirtyRegions()) {
        checkBackgroundStyles(renderMatrix);
      }
    }
    return;
  }
  bitFields.hasBackgroundStyle = false;
  if (!_layerStyles.empty() || !_filters.empty()) {
    auto contentScale = renderMatrix.getMaxScale();
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
      *contentBounds = renderMatrix.mapRect(content->getBounds());
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
    childMatrix.postConcat(renderMatrix);
    auto childTransformer = transformer;
    if (child->_scrollRect) {
      auto childScrollRect = childMatrix.mapRect(*child->_scrollRect);
      childTransformer = RegionTransformer::MakeFromClip(childScrollRect, childTransformer);
    }
    auto childForceDirty = forceDirty || child->bitFields.dirtyTransform;
    child->updateRenderBounds(childMatrix, childTransformer, childForceDirty);
    child->bitFields.dirtyTransform = false;
    if (!child->maskOwner) {
      renderBounds.join(child->renderBounds);
    }
  }
  auto hasBackgroundStyle = false;
  for (auto& style : _layerStyles) {
    if (style->extraSourceType() == LayerStyleExtraSourceType::Background) {
      hasBackgroundStyle = true;
      break;
    }
  }
  if (hasBackgroundStyle) {
    bitFields.hasBackgroundStyle = true;
    propagateHasBackgroundStyleFlags();
    updateBackgroundBounds(renderMatrix);
  }
  bitFields.dirtyDescendents = false;
}

void Layer::checkBackgroundStyles(const Matrix& renderMatrix) {
  for (auto& child : _children) {
    if (!child->bitFields.hasBackgroundStyle || !child->bitFields.visible || child->_alpha <= 0) {
      continue;
    }
    auto childMatrix = child->getMatrixWithScrollRect();
    childMatrix.postConcat(renderMatrix);
    child->checkBackgroundStyles(childMatrix);
  }
  updateBackgroundBounds(renderMatrix);
}

void Layer::updateBackgroundBounds(const Matrix& renderMatrix) {
  auto contentScale = renderMatrix.getMaxScale();
  auto backgroundChanged = false;
  for (auto& style : _layerStyles) {
    if (style->extraSourceType() != LayerStyleExtraSourceType::Background) {
      continue;
    }
    if (_root->invalidateBackground(renderBounds, style.get(), contentScale)) {
      backgroundChanged = true;
    }
  }
  if (backgroundChanged) {
    auto layer = this;
    while (layer && !layer->bitFields.dirtyDescendents) {
      layer->rasterizedContent = nullptr;
      if (layer->maskOwner) {
        break;
      }
      layer = layer->_parent;
    }
  }
}

void Layer::propagateHasBackgroundStyleFlags() {
  auto layer = _parent;
  while (layer && !layer->bitFields.hasBackgroundStyle) {
    layer->bitFields.hasBackgroundStyle = true;
    layer = layer->_parent;
  }
}

bool Layer::hasDescendantBackgroundStyle() {
  for (const auto& style : _layerStyles) {
    if (style->extraSourceType() == LayerStyleExtraSourceType::Background) {
      return true;
    }
  }
  for (const auto& child : _children) {
    if (child->hasDescendantBackgroundStyle()) {
      return true;
    }
  }
  return false;
}

}  // namespace tgfx
