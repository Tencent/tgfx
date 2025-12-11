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
#include <cmath>
#include "core/Matrix2D.h"
#include "core/filters/Transform3DImageFilter.h"
#include "core/images/TextureImage.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/ContourContext.h"
#include "layers/DrawArgs.h"
#include "layers/RegionTransformer.h"
#include "layers/RootLayer.h"
#include "layers/SubTreeCache.h"
#include "layers/contents/LayerContent.h"
#include "layers/contents/RasterizedContent.h"
#include "layers/filters/Transform3DFilter.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/ShapeLayer.h"

namespace tgfx {
// The minimum size (longest edge) for sub-tree cache. This prevents creating excessively small
// mipmap levels that would be inefficient to cache.
static constexpr int SUB_TREE_CACHE_MIN_SIZE = 32;
static std::atomic_bool AllowsEdgeAntialiasing = true;
static std::atomic_bool AllowsGroupOpacity = false;

struct LayerStyleSource {
  float contentScale = 1.0f;
  std::shared_ptr<Image> content = nullptr;
  Point contentOffset = {};
  std::shared_ptr<Image> contour = nullptr;
  Point contourOffset = {};
};

// Determine if the 4*4 matrix contains only 2D affine transformations, i.e., no Z-axis related
// transformations or projection transformations
static bool IsMatrix3DAffine(const Matrix3D& matrix) {
  return FloatNearlyZero(matrix.getRowColumn(0, 2)) && FloatNearlyZero(matrix.getRowColumn(1, 2)) &&
         matrix.getRow(2) == Vec4(0, 0, 1, 0) && matrix.getRow(3) == Vec4(0, 0, 0, 1);
}

// When a 4x4 matrix does not contain Z-axis related transformations and projection transformations,
// this function returns an equivalent 2D affine transformation. Otherwise, the return value will
// lose information about Z-axis related transformations and projection transformations.
static Matrix GetMayLossyAffineMatrix(const Matrix3D& matrix) {
  auto affineMatrix = Matrix::I();
  affineMatrix.setAll(matrix.getRowColumn(0, 0), matrix.getRowColumn(0, 1),
                      matrix.getRowColumn(0, 3), matrix.getRowColumn(1, 0),
                      matrix.getRowColumn(1, 1), matrix.getRowColumn(1, 3));
  return affineMatrix;
}

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
  PictureRecorder recorder = {};
  auto contentCanvas = recorder.beginRecording();
  contentCanvas->scale(contentScale, contentScale);
  drawFunction(contentCanvas);
  return recorder.finishRecordingAsPicture();
}

static std::shared_ptr<Image> ToImageWithOffset(
    std::shared_ptr<Picture> picture, Point* offset, const Rect* imageBounds = nullptr,
    std::shared_ptr<ColorSpace> colorSpace = ColorSpace::SRGB()) {
  if (picture == nullptr) {
    return nullptr;
  }
  auto bounds = imageBounds ? *imageBounds : picture->getBounds();
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto image =
      Image::MakeFrom(std::move(picture), static_cast<int>(ceilf(bounds.width())),
                      static_cast<int>(ceilf(bounds.height())), &matrix, std::move(colorSpace));
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

static std::shared_ptr<Image> MakeImageWithTransform(std::shared_ptr<Image> image,
                                                     const Matrix3D* transform3D,
                                                     Matrix* imageMatrix) {
  DEBUG_ASSERT(imageMatrix);
  if (transform3D == nullptr) {
    return image;
  }
  auto adaptedMatrix = *transform3D;
  auto offsetMatrix =
      Matrix3D::MakeTranslate(imageMatrix->getTranslateX(), imageMatrix->getTranslateY(), 0);
  auto invOffsetMatrix =
      Matrix3D::MakeTranslate(-imageMatrix->getTranslateX(), -imageMatrix->getTranslateY(), 0);
  auto scaleMatrix = Matrix3D::MakeScale(imageMatrix->getScaleX(), imageMatrix->getScaleY(), 1.0f);
  auto invScaleMatrix =
      Matrix3D::MakeScale(1.0f / imageMatrix->getScaleX(), 1.0f / imageMatrix->getScaleY(), 1.0f);
  adaptedMatrix = invScaleMatrix * invOffsetMatrix * adaptedMatrix * offsetMatrix * scaleMatrix;
  auto imageFilter = ImageFilter::Transform3D(adaptedMatrix);
  auto offset = Point();
  image = image->makeWithFilter(imageFilter, &offset);
  imageMatrix->preTranslate(offset.x, offset.y);
  return image;
}

static int GetMipmapCacheLongEdge(int maxSize, float contentScale, const Rect& layerBounds) {
  auto maxBoundsSize = std::max(layerBounds.width(), layerBounds.height());
  auto scaleBoundsSize = static_cast<int>(ceilf(maxBoundsSize * contentScale));
  if (scaleBoundsSize > maxSize) {
    return scaleBoundsSize;
  }
  if (SUB_TREE_CACHE_MIN_SIZE >= maxSize) {
    return maxSize;
  }
  auto targetSize = std::max(scaleBoundsSize, SUB_TREE_CACHE_MIN_SIZE);
  auto currentLongEdge = maxSize;
  while ((currentLongEdge >> 1) >= targetSize) {
    currentLongEdge >>= 1;
  }
  return currentLongEdge;
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
  bitFields.matrix3DIsAffine = true;
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

Point Layer::position() const {
  if (bitFields.matrix3DIsAffine) {
    return {_matrix3D.getRowColumn(0, 3), _matrix3D.getRowColumn(1, 3)};
  }

  auto result = matrix3D().mapVec3(Vec3(0, 0, 0));
  return {result.x, result.y};
}

void Layer::setPosition(const Point& value) {
  if (bitFields.matrix3DIsAffine) {
    if (_matrix3D.getTranslateX() == value.x && _matrix3D.getTranslateY() == value.y) {
      return;
    }
    _matrix3D.setRowColumn(0, 3, value.x);
    _matrix3D.setRowColumn(1, 3, value.y);
  } else {
    auto curPos = _matrix3D.mapVec3(Vec3(0, 0, 0));
    if (FloatNearlyEqual(curPos.x, value.x) && FloatNearlyEqual(curPos.y, value.y)) {
      return;
    }
    _matrix3D.postTranslate(value.x - curPos.x, value.y - curPos.y, 0);
  }
  invalidateTransform();
}

Matrix Layer::matrix() const {
  auto affineMatrix = Matrix::I();
  if (!bitFields.matrix3DIsAffine) {
    return Matrix::I();
  }
  affineMatrix.setAll(_matrix3D.getRowColumn(0, 0), _matrix3D.getRowColumn(0, 1),
                      _matrix3D.getRowColumn(0, 3), _matrix3D.getRowColumn(1, 0),
                      _matrix3D.getRowColumn(1, 1), _matrix3D.getRowColumn(1, 3));
  return affineMatrix;
}

void Layer::setMatrix(const Matrix& value) {
  const Matrix3D value3D(value);
  if (value3D == _matrix3D) {
    return;
  }
  _matrix3D = value3D;
  bitFields.matrix3DIsAffine = true;
  invalidateTransform();
}

void Layer::setMatrix3D(const Matrix3D& value) {
  if (value == _matrix3D) {
    return;
  }
  _matrix3D = value;
  bitFields.matrix3DIsAffine = IsMatrix3DAffine(value);
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
  invalidateCache();
  localBounds = nullptr;
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
  invalidateCache();
  localBounds = nullptr;
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

Rect Layer::getBoundsInternal(const Matrix3D& coordinateMatrix, bool computeTightBounds) {
  if (computeTightBounds || bitFields.dirtyDescendents) {
    return computeBounds(coordinateMatrix, computeTightBounds);
  }
  if (!localBounds) {
    localBounds = std::make_unique<Rect>(computeBounds(Matrix3D::I(), computeTightBounds));
  }
  auto result = coordinateMatrix.mapRect(*localBounds);
  if (!IsMatrix3DAffine(coordinateMatrix)) {
    // while the matrix is not affine, the layer will draw with a 3D filter, so the bounds should
    // round out.
    result.roundOut();
  }
  return result;
}

Rect Layer::computeBounds(const Matrix3D& coordinateMatrix, bool computeTightBounds) {
  // If the matrix only contains 2D affine transformations, directly use the equivalent 2D
  // transformation matrix to calculate the final Bounds
  bool isCoordinateMatrixAffine = IsMatrix3DAffine(coordinateMatrix);
  auto workAffineMatrix =
      isCoordinateMatrixAffine ? GetMayLossyAffineMatrix(coordinateMatrix) : Matrix::I();
  auto workMatrix3D = isCoordinateMatrixAffine ? Matrix3D(workAffineMatrix) : coordinateMatrix;

  Rect bounds = {};
  if (auto content = getContent()) {
    if (computeTightBounds) {
      bounds.join(content->getTightBounds(workAffineMatrix));
    } else {
      bounds.join(workAffineMatrix.mapRect(content->getBounds()));
    }
  }

  for (const auto& child : _children) {
    // Alpha does not need to be checked; alpha == 0 is still valid.
    if (!child->visible() || child->maskOwner) {
      continue;
    }
    auto childMatrix = child->getMatrixWithScrollRect();
    childMatrix.postConcat(workMatrix3D);
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
    auto contentScale = workAffineMatrix.getMaxScale();
    auto layerBounds = bounds;
    for (auto& layerStyle : _layerStyles) {
      auto styleBounds = layerStyle->filterBounds(layerBounds, contentScale);
      bounds.join(styleBounds);
    }
    for (auto& filter : _filters) {
      bounds = filter->filterBounds(bounds, contentScale);
    }
  }

  if (isCoordinateMatrixAffine) {
    return bounds;
  }

  // If the matrix contains Z-axis transformations and projection transformations, first calculate
  // the Bounds using the identity matrix, then apply the 3D transformation to the result.
  // Otherwise, use the equivalent affine transformation matrix to calculate the Bounds.
  bounds = coordinateMatrix.mapRect(bounds);
  bounds.roundOut();
  return bounds;
}

Point Layer::globalToLocal(const Point& globalPoint) const {
  auto globalMatrix = getGlobalMatrix();
  // All vertices inside the rect have an initial z-coordinate of 0, so the third column of the 4x4
  // matrix does not affect the final transformation result and can be ignored. Additionally, since
  // we do not care about the final projected z-axis coordinate, the third row can also be ignored.
  // Therefore, the 4x4 matrix can be simplified to a 3x3 matrix.
  float values[16] = {};
  globalMatrix.getColumnMajor(values);
  auto matrix2D = Matrix2D::MakeAll(values[0], values[1], values[3], values[4], values[5],
                                    values[7], values[12], values[13], values[15]);
  Matrix2D inversedMatrix;
  if (!matrix2D.invert(&inversedMatrix)) {
    DEBUG_ASSERT(false);
    return Point::Make(0, 0);
  }
  auto result = inversedMatrix.mapVec2({globalPoint.x, globalPoint.y});
  return {result.x, result.y};
}

Point Layer::localToGlobal(const Point& localPoint) const {
  auto globalMatrix = getGlobalMatrix();
  auto result = globalMatrix.mapVec3({localPoint.x, localPoint.y, 0});
  return {result.x, result.y};
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
  auto globalToLocalMatrix = Matrix3D::I();
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
  }

  if (context && canInvert && hasBackgroundStyle()) {
    auto scale = canvas->getMatrix().getMaxScale();
    auto backgroundRect = clippedBounds;
    backgroundRect.scale(scale, scale);
    auto backgroundMatrix = Matrix::I();
    if (IsMatrix3DAffine(globalToLocalMatrix)) {
      // If the transformation from the current layer node to the root node only contains 2D affine
      // transformations, then draw the real layer background and calculate the accurate
      // transformation matrix of the background image in the layer's local coordinate system
      backgroundMatrix = GetMayLossyAffineMatrix(globalToLocalMatrix);
    } else {
      // Otherwise, it's impossible to draw an accurate background. Only the background image
      // corresponding to the minimum bounding rectangle of the layer node subtree after drawing can
      // be drawn, and this rectangle is stretched to fill the layer area. Based on this, Calculate
      // the transformation matrix for drawing the background image within renderBounds to bounds.
      DEBUG_ASSERT(!FloatNearlyZero(renderRect.width()) && !FloatNearlyZero(renderRect.height()));
      backgroundMatrix = Matrix::MakeTrans(-renderRect.left, -renderRect.top);
      backgroundMatrix.postScale(bounds.width() / renderRect.width(),
                                 bounds.height() / renderRect.height());
      backgroundMatrix.postTranslate(bounds.left, bounds.top);
    }
    backgroundMatrix.postScale(scale, scale);
    if (auto backgroundContext =
            createBackgroundContext(context, backgroundRect, backgroundMatrix,
                                    bounds == clippedBounds, surface->colorSpace())) {
      auto backgroundCanvas = backgroundContext->getCanvas();
      auto actualMatrix = backgroundCanvas->getMatrix();
      bool isLocalToGlobalAffine = IsMatrix3DAffine(localToGlobalMatrix);
      if (isLocalToGlobalAffine) {
        // The current layer node to the root node only contains 2D affine transformations, need to
        // superimpose the transformation matrix that maps layer coordinates to the actual drawing
        // area.
        // Since the background recorder starts from the current layer, we need to pre-concatenate
        // localToGlobalMatrix to the background canvas matrix to ensure the coordinate space is
        // correct.
        actualMatrix.preConcat(GetMayLossyAffineMatrix(localToGlobalMatrix));
      } else {
        // Otherwise, need to superimpose the transformation matrix that maps the bounds to the
        // actual drawing area renderRect.
        DEBUG_ASSERT(!FloatNearlyZero(bounds.width()) && !FloatNearlyZero(bounds.height()));
        auto toBackgroundMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
        toBackgroundMatrix.postScale(renderRect.width() / bounds.width(),
                                     renderRect.height() / bounds.height());
        toBackgroundMatrix.postTranslate(renderRect.left, renderRect.top);
        actualMatrix.preConcat(toBackgroundMatrix);
      }
      backgroundCanvas->setMatrix(actualMatrix);
      Point offset = {};
      // If there are 3D transformations or projection transformations from the current layer node
      // to the root node, it's impossible to obtain an accurate background image and stretch it
      // into a rectangle. In this case, obtain the background image corresponding to the minimum
      // bounding rectangle of the current layer subtree after drawing.
      auto image = isLocalToGlobalAffine ? getBackgroundImage(args, scale, &offset)
                                         : getBoundsBackgroundImage(args, scale, &offset);
      if (image != nullptr) {
        AutoCanvasRestore autoRestore(backgroundCanvas);
        actualMatrix.preScale(1.0f / scale, 1.0f / scale);
        backgroundCanvas->setMatrix(actualMatrix);
        backgroundCanvas->drawImage(image, offset.x, offset.y);
      }
      args.blurBackground = std::move(backgroundContext);
    }
  }
  drawLayer(args, canvas, alpha, blendMode, nullptr);
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
  invalidateCache();
  localBounds = nullptr;
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

Matrix3D Layer::getGlobalMatrix() const {
  // The global matrix transforms the layer's local coordinate space to the coordinate space of its
  // top-level parent layer. This means the top-level parent layer's own matrix is not included in
  // the global matrix.
  Matrix3D matrix = {};
  auto layer = this;
  while (layer->_parent) {
    matrix.postConcat(layer->getMatrixWithScrollRect());
    layer = layer->_parent;
  }
  return matrix;
}

Matrix3D Layer::getMatrixWithScrollRect() const {
  auto matrix = _matrix3D;
  if (_scrollRect) {
    matrix.preTranslate(-_scrollRect->left, -_scrollRect->top, 0);
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
      (args.drawMode == DrawMode::Background && hasBackgroundStyle()) ||
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

void Layer::drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                      const Matrix3D* transform3D) {
  DEBUG_ASSERT(canvas != nullptr);
  auto contentScale = canvas->getMatrix().getMaxScale();
  if (FloatNearlyZero(contentScale)) {
    return;
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
      drawLayer(backgroundArgs, args.blurBackground->getCanvas(), alpha, blendMode, transform3D);
    }
    return;
  }
  if (drawWithCache(args, canvas, alpha, blendMode, transform3D)) {
    return;
  }
  if (blendMode != BlendMode::SrcOver || !bitFields.passThroughBackground ||
      (alpha < 1.0f && bitFields.allowsGroupOpacity) || bitFields.shouldRasterize ||
      (!_filters.empty() && !args.excludeEffects) || hasValidMask() || transform3D != nullptr) {
    drawOffscreen(args, canvas, alpha, blendMode, transform3D);
  } else {
    // draw directly
    drawDirectly(args, canvas, alpha);
  }
}

Matrix3D Layer::getRelativeMatrix(const Layer* targetCoordinateSpace) const {
  if (targetCoordinateSpace == nullptr || targetCoordinateSpace == this) {
    return {};
  }
  auto targetLayerMatrix = targetCoordinateSpace->getGlobalMatrix();
  Matrix3D targetLayerInverseMatrix = {};
  if (!targetLayerMatrix.invert(&targetLayerInverseMatrix)) {
    return {};
  }
  Matrix3D relativeMatrix = getGlobalMatrix();
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
  // When the mask's transformation matrix does not contain 3D and projection transformations,
  // affineRelativeMatrix is an equivalent matrix. Otherwise, directly use the identity matrix to
  // draw the mask content, and this 3D matrix will be applied through a filter during drawing
  auto isMatrixAffine = IsMatrix3DAffine(relativeMatrix);
  auto affineRelativeMatrix =
      isMatrixAffine ? GetMayLossyAffineMatrix(relativeMatrix) : Matrix::I();
  auto maskClipBounds = layerClipBounds;
  if (layerClipBounds.has_value()) {
    auto invertedMatrix = Matrix::I();
    if (affineRelativeMatrix.invert(&invertedMatrix)) {
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
  if (!isMatrixAffine) {
    maskContentImage = maskContentImage->makeWithFilter(ImageFilter::Transform3D(relativeMatrix));
  }
  affineRelativeMatrix.preScale(1.0f / scale, 1.0f / scale);
  affineRelativeMatrix.preTranslate(maskImageOffset.x, maskImageOffset.y);

  auto shader = Shader::MakeImageShader(maskContentImage, TileMode::Decal, TileMode::Decal);
  if (shader) {
    shader = shader->makeWithMatrix(affineRelativeMatrix);
  }
  return MaskFilter::MakeShader(shader);
}

std::shared_ptr<Image> Layer::getContentImage(const DrawArgs& contentArgs, float contentScale,
                                              const std::shared_ptr<Image>& passThroughImage,
                                              const Matrix& passThroughImageMatrix,
                                              std::optional<Rect> clipBounds, Matrix* imageMatrix) {
  DEBUG_ASSERT(imageMatrix);
  auto inputBounds = getBounds();
  if (clipBounds.has_value()) {
    if (!contentArgs.excludeEffects) {
      // clipBounds is in local coordinate space,  so we getImageFilter with scale 1.0f.
      auto filter = getImageFilter(1.0f);
      if (filter) {
        clipBounds = filter->filterBounds(*clipBounds, MapDirection::Reverse);
      }
    }
    if (!inputBounds.intersect(*clipBounds)) {
      return nullptr;
    }
  }

  // When drawing offscreen, if the layer contains 3D transformations, the background cannot be
  // accurately stretched to fill a rectangle, so the background is drawn separately.
  std::unordered_set<LayerStyleExtraSourceType> styleExtraSourceTypes = {
      LayerStyleExtraSourceType::None, LayerStyleExtraSourceType::Contour};
  if (bitFields.matrix3DIsAffine) {
    styleExtraSourceTypes.insert(LayerStyleExtraSourceType::Background);
  }

  std::shared_ptr<Image> finalImage = nullptr;
  auto context = contentArgs.context;
  if (context && passThroughImage) {
    auto surfaceRect = passThroughImageMatrix.mapRect(inputBounds);
    surfaceRect.roundOut();
    surfaceRect.intersect(Rect::MakeWH(passThroughImage->width(), passThroughImage->height()));
    auto offscreenSurface = Surface::Make(context, static_cast<int>(surfaceRect.width()),
                                          static_cast<int>(surfaceRect.height()), false, 1, false,
                                          0, contentArgs.dstColorSpace);
    if (!offscreenSurface) {
      return nullptr;
    }
    auto offscreenCanvas = offscreenSurface->getCanvas();
    offscreenCanvas->translate(-surfaceRect.left, -surfaceRect.top);
    offscreenCanvas->drawImage(passThroughImage);
    offscreenCanvas->concat(passThroughImageMatrix);
    drawDirectly(contentArgs, offscreenCanvas, 1.0f, styleExtraSourceTypes);
    finalImage = offscreenSurface->makeImageSnapshot();
    offscreenCanvas->getMatrix().invert(imageMatrix);
  } else {
    PictureRecorder recorder = {};
    auto offscreenCanvas = recorder.beginRecording();
    offscreenCanvas->scale(contentScale, contentScale);
    offscreenCanvas->clipRect(inputBounds);
    if (passThroughImage) {
      AutoCanvasRestore offscreenRestore(offscreenCanvas);
      offscreenCanvas->concat(passThroughImageMatrix);
      offscreenCanvas->drawImage(passThroughImage);
    }
    drawDirectly(contentArgs, offscreenCanvas, 1.0f, styleExtraSourceTypes);
    Point offset;
    finalImage = ToImageWithOffset(recorder.finishRecordingAsPicture(), &offset, nullptr,
                                   contentArgs.dstColorSpace);
    imageMatrix->setScale(1.0f / contentScale, 1.0f / contentScale);
    imageMatrix->preTranslate(offset.x, offset.y);
  }

  if (!finalImage) {
    return nullptr;
  }

  auto filterOffset = Point::Make(0, 0);
  if (!contentArgs.excludeEffects) {
    auto filter = getImageFilter(contentScale);
    if (filter) {
      std::optional<Rect> filterClipBounds = std::nullopt;
      if (clipBounds.has_value()) {
        auto invertMatrix = Matrix::I();
        imageMatrix->invert(&invertMatrix);
        filterClipBounds = invertMatrix.mapRect(*clipBounds);
        filterClipBounds->roundOut();
      }
      // clipBounds may be smaller than the image bounds, so we need to pass it to makeWithFilter.
      finalImage = finalImage->makeWithFilter(
          filter, &filterOffset, filterClipBounds.has_value() ? &*filterClipBounds : nullptr);
      imageMatrix->preTranslate(filterOffset.x, filterOffset.y);
    }
  }
  return finalImage;
}

bool Layer::shouldPassThroughBackground(BlendMode blendMode, const Matrix3D* transform3D) const {
  return bitFields.passThroughBackground && blendMode == BlendMode::SrcOver && _filters.empty() &&
         bitFields.hasBlendMode && transform3D == nullptr;
}

bool Layer::canUseSubTreeCache(int subTreeCacheMaxSize, BlendMode blendMode,
                               const Matrix3D* transform3D) {
  if (subTreeCache) {
    return true;
  }
  if (subTreeCacheMaxSize <= 0) {
    return false;
  }
  if (_children.empty() && _layerStyles.empty() && _filters.empty()) {
    return false;
  }
  if (shouldPassThroughBackground(blendMode, transform3D) || hasBackgroundStyle()) {
    return false;
  }
  // Skip caching on the first render to avoid caching content that is only displayed once.
  // The cache is created on the second render when the layer is confirmed to be reused.
  subTreeCache = std::make_unique<SubTreeCache>();
  return false;
}

std::shared_ptr<Image> Layer::createSubTreeCacheImage(const DrawArgs& args, float contentScale,
                                                      const Rect& layerBounds,
                                                      Matrix* drawingMatrix) {
  DEBUG_ASSERT(drawingMatrix != nullptr);
  if (FloatNearlyZero(contentScale)) {
    return nullptr;
  }

  auto drawArgs = args;
  drawArgs.renderFlags |= RenderFlags::DisableCache;
  drawArgs.renderRect = nullptr;
  drawArgs.blurBackground = nullptr;

  auto picture = RecordPicture(drawArgs.drawMode, contentScale,
                               [&](Canvas* canvas) { drawDirectly(drawArgs, canvas, 1.0f); });
  if (!picture) {
    return nullptr;
  }
  auto scaledBounds = layerBounds;
  scaledBounds.scale(contentScale, contentScale);
  Point offset = {};
  auto image = ToImageWithOffset(std::move(picture), &offset, &scaledBounds, args.dstColorSpace);
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

  return image->makeTextureImage(args.context);
}

bool Layer::drawWithCache(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                          const Matrix3D* transform3D) {
  if (args.drawMode != DrawMode::Normal || args.excludeEffects) {
    return false;
  }
  RasterizedContent* cache = nullptr;
  if (auto rasterizedCache = getRasterizedCache(args, canvas->getMatrix())) {
    cache = rasterizedCache;
  }
  if (!cache) {
    if (!canUseSubTreeCache(args.subTreeCacheMaxSize, blendMode, transform3D)) {
      return false;
    }
    return drawWithSubTreeCache(args, canvas, alpha, blendMode, transform3D);
  }
  std::optional<Rect> clipBounds = std::nullopt;
  std::shared_ptr<MaskFilter> maskFilter = nullptr;
  auto contentScale = canvas->getMatrix().getMaxScale();
  if (hasValidMask()) {
    clipBounds = GetClipBounds(args.blurBackground ? args.blurBackground->getCanvas() : canvas);
    maskFilter = getMaskFilter(args, contentScale, clipBounds);
    if (maskFilter == nullptr) {
      return true;
    }
  }
  cache->draw(canvas, bitFields.allowsEdgeAntialiasing, alpha, maskFilter, blendMode, transform3D);
  if (args.blurBackground) {
    if (!hasBackgroundStyle()) {
      cache->draw(args.blurBackground->getCanvas(), bitFields.allowsEdgeAntialiasing, alpha,
                  maskFilter, blendMode, transform3D);
    } else {
      auto backgroundArgs = args;
      backgroundArgs.drawMode = DrawMode::Background;
      backgroundArgs.blurBackground = nullptr;
      drawOffscreen(backgroundArgs, args.blurBackground->getCanvas(), alpha, blendMode,
                    transform3D);
    }
  }
  return true;
}

SubTreeCache* Layer::getValidSubTreeCache(const DrawArgs& args, int longEdge,
                                          const Rect& layerBounds) {
  if (subTreeCache->valid(args.context, longEdge)) {
    return subTreeCache.get();
  }
  if (args.renderFlags & RenderFlags::DisableCache || longEdge > args.subTreeCacheMaxSize) {
    return nullptr;
  }
  auto maxBoundsSize = std::max(layerBounds.width(), layerBounds.height());
  if (FloatNearlyZero(maxBoundsSize)) {
    return nullptr;
  }
  auto cacheScale = static_cast<float>(longEdge) / maxBoundsSize;
  auto imageMatrix = Matrix::I();
  auto image = createSubTreeCacheImage(args, cacheScale, layerBounds, &imageMatrix);
  if (image == nullptr) {
    return nullptr;
  }
  auto textureProxy = std::static_pointer_cast<TextureImage>(image)->getTextureProxy();
  DEBUG_ASSERT(longEdge == std::max(textureProxy->width(), textureProxy->height()));
  subTreeCache->addCache(args.context, textureProxy, imageMatrix, args.dstColorSpace);
  return subTreeCache.get();
}

bool Layer::drawWithSubTreeCache(const DrawArgs& args, Canvas* canvas, float alpha,
                                 BlendMode blendMode, const Matrix3D* transform3D) {
  auto layerBounds = getBounds();
  auto contentScale = canvas->getMatrix().getMaxScale();
  auto longEdge = GetMipmapCacheLongEdge(args.subTreeCacheMaxSize, contentScale, layerBounds);
  auto cache = getValidSubTreeCache(args, longEdge, layerBounds);
  if (cache == nullptr) {
    return false;
  }
  Paint paint = {};
  if (hasValidMask()) {
    auto clipBounds =
        GetClipBounds(args.blurBackground ? args.blurBackground->getCanvas() : canvas);
    auto maskFilter = getMaskFilter(args, contentScale, clipBounds);
    if (maskFilter == nullptr) {
      return true;
    }
    paint.setMaskFilter(maskFilter);
  }
  paint.setAntiAlias(bitFields.allowsEdgeAntialiasing);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  cache->draw(args.context, longEdge, canvas, paint, transform3D);
  if (args.blurBackground) {
    cache->draw(args.context, longEdge, args.blurBackground->getCanvas(), paint, transform3D);
  }
  return true;
}

void Layer::drawOffscreen(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                          const Matrix3D* transform3D) {
  if (transform3D != nullptr) {
    drawBackgroundLayerStyles(args, canvas, alpha, *transform3D);
  }

  auto contentScale = canvas->getMatrix().getMaxScale();

  auto contentArgs = args;
  contentArgs.blurBackground = args.blurBackground && hasBackgroundStyle()
                                   ? args.blurBackground->createSubContext(renderBounds, true)
                                   : nullptr;

  Matrix passthroughImageMatrix = Matrix::I();
  std::shared_ptr<Image> passthroughImage = nullptr;
  if (shouldPassThroughBackground(blendMode, transform3D)) {
    if (canvas->getSurface()) {
      passthroughImage = canvas->getSurface()->makeImageSnapshot();
      passthroughImageMatrix = canvas->getMatrix();
    }
  }

  auto clipBounds = GetClipBounds(args.blurBackground ? args.blurBackground->getCanvas() : canvas);
  auto contentClipBounds = clipBounds;
  if (transform3D != nullptr && clipBounds.has_value()) {
    auto filter = ImageFilter::Transform3D(*transform3D);
    if (filter) {
      contentClipBounds = filter->filterBounds(*clipBounds, MapDirection::Reverse);
    }
  }

  auto imageMatrix = Matrix::I();
  auto image = getContentImage(contentArgs, contentScale, passthroughImage, passthroughImageMatrix,
                               contentClipBounds, &imageMatrix);

  auto invertImageMatrix = Matrix::I();
  if (image == nullptr || !imageMatrix.invert(&invertImageMatrix)) {
    return;
  }

  image = MakeImageWithTransform(image, transform3D, &imageMatrix);

  if (args.blurBackground && !contentArgs.blurBackground) {
    image = image->makeRasterized();
  }

  Paint paint = {};
  paint.setAntiAlias(bitFields.allowsEdgeAntialiasing);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);

  std::shared_ptr<MaskFilter> maskFilter = nullptr;
  if (hasValidMask()) {
    maskFilter = getMaskFilter(args, contentScale, clipBounds);
    // if mask filter is nullptr while mask is valid, that means the layer is not visible.
    if (!maskFilter) {
      return;
    }
    paint.setMaskFilter(maskFilter->makeWithMatrix(invertImageMatrix));
  }

  AutoCanvasRestore autoRestore(canvas);
  canvas->concat(imageMatrix);
  canvas->drawImage(image, &paint);
  if (args.blurBackground) {
    if (contentArgs.blurBackground) {
      auto filter = getImageFilter(contentScale);
      paint.setImageFilter(filter);
      paint.setMaskFilter(maskFilter);
      contentArgs.blurBackground->drawToParent(paint);
    } else {
      auto backgroundCanvas = args.blurBackground->getCanvas();
      AutoCanvasRestore autoRestoreBg(backgroundCanvas);
      backgroundCanvas->concat(imageMatrix);
      backgroundCanvas->drawImage(image, &paint);
    }
  }

  // There is no scenario where LayerStyle's Position and ExtraSourceType are 'above' and
  // 'background' respectively at the same time, so no special handling is needed after drawing the
  // content.
}

void Layer::drawDirectly(const DrawArgs& args, Canvas* canvas, float alpha) {
  std::unordered_set<LayerStyleExtraSourceType> styleExtraSourceTypes = {
      LayerStyleExtraSourceType::None, LayerStyleExtraSourceType::Contour,
      LayerStyleExtraSourceType::Background};
  drawDirectly(args, canvas, alpha, styleExtraSourceTypes);
}

void Layer::drawDirectly(const DrawArgs& args, Canvas* canvas, float alpha,
                         const std::unordered_set<LayerStyleExtraSourceType>& extraSourceTypes) {
  auto layerStyleSource = getLayerStyleSource(args, canvas->getMatrix());
  drawContents(args, canvas, alpha, layerStyleSource.get(), nullptr, extraSourceTypes);
}

class LayerBrushModifier : public BrushModifier {
 public:
  LayerBrushModifier(bool antiAlias, float alpha) : antiAlias(antiAlias), alpha(alpha) {
  }

  Brush transform(const Brush& brush) const override {
    auto newBrush = brush;
    newBrush.color.alpha *= alpha;
    newBrush.antiAlias = antiAlias;
    return newBrush;
  }

 private:
  bool antiAlias = true;
  float alpha = 1.0f;
};

void Layer::drawContents(const DrawArgs& args, Canvas* canvas, float alpha,
                         const LayerStyleSource* layerStyleSource, const Layer* stopChild,
                         const std::unordered_set<LayerStyleExtraSourceType>& extraSourceTypes) {
  if (layerStyleSource) {
    drawLayerStyles(args, canvas, alpha, layerStyleSource, LayerStylePosition::Below,
                    extraSourceTypes);
  }
  LayerBrushModifier layerFill(bitFields.allowsEdgeAntialiasing, alpha);
  auto content = getContent();
  if (content) {
    if (args.drawMode == DrawMode::Contour) {
      content->drawContour(canvas, &layerFill);
    } else {
      content->drawDefault(canvas, &layerFill);
      if (args.blurBackground) {
        content->drawDefault(args.blurBackground->getCanvas(), &layerFill);
      }
    }
  }
  if (!drawChildren(args, canvas, alpha, stopChild)) {
    return;
  }
  if (layerStyleSource) {
    drawLayerStyles(args, canvas, alpha, layerStyleSource, LayerStylePosition::Above,
                    extraSourceTypes);
  }
  if (content && args.drawMode != DrawMode::Contour) {
    content->drawForeground(canvas, &layerFill);
    if (args.blurBackground) {
      content->drawForeground(args.blurBackground->getCanvas(), &layerFill);
    }
  }
}

bool Layer::drawChildren(const DrawArgs& args, Canvas* canvas, float alpha,
                         const Layer* stopChild) {
  int lastBackgroundLayerIndex = -1;
  if (args.forceDrawBackground) {
    // lastBackgroundLayerIndex must cover all children
    lastBackgroundLayerIndex = static_cast<int>(_children.size());
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
    auto childMatrix = child->getMatrixWithScrollRect();
    // If the sublayer's Matrix contains 3D transformations or projection transformations, then
    // treat its Matrix as an identity matrix here, and let the sublayer handle its actual position
    // through 3D filter methods,
    const bool isChildMatrixAffine = IsMatrix3DAffine(childMatrix);
    auto childAffineMatrix =
        isChildMatrixAffine ? GetMayLossyAffineMatrix(childMatrix) : Matrix::I();
    canvas->concat(childAffineMatrix);
    auto clipChildScrollRectHandler = [&](Canvas& clipCanvas) {
      if (child->_scrollRect) {
        // If the sublayer's Matrix contains 3D transformations or projection transformations, then
        // because this matrix has been merged into the Canvas, it can be directly completed through
        // the clipping rectangle. Otherwise, the canvas will not contain this matrix information,
        // and clipping needs to be done by transforming the Path.
        if (isChildMatrixAffine) {
          clipCanvas.clipRect(*child->_scrollRect);
        } else {
          auto path = Path();
          path.addRect(*(child->_scrollRect));
          path.transform3D(childMatrix);
          clipCanvas.clipPath(path);
        }
      }
    };
    if (child->_scrollRect) {
      clipChildScrollRectHandler(*canvas);
    }
    if (backgroundCanvas) {
      backgroundCanvas->save();
      backgroundCanvas->concat(childAffineMatrix);
      clipChildScrollRectHandler(*backgroundCanvas);
    }

    auto transform = isChildMatrixAffine ? nullptr : &childMatrix;
    child->drawLayer(childArgs, canvas, child->_alpha * alpha,
                     static_cast<BlendMode>(child->bitFields.blendMode), transform);
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
  // If the layer's Matrix contains 3D transformations or projection transformations, since this
  // matrix has already been merged into the Canvas, clipping can be done directly through the
  // clipping rectangle. Otherwise, the canvas does not carry this matrix information, and clipping
  // needs to be performed by transforming the Path.
  if (bitFields.matrix3DIsAffine) {
    canvas->concat(GetMayLossyAffineMatrix(getMatrixWithScrollRect()));
    if (_scrollRect) {
      canvas->clipRect(*_scrollRect);
    }
  } else if (_scrollRect) {
    auto path = Path();
    path.addRect(*_scrollRect);
    path.transform3D(getMatrixWithScrollRect());
    canvas->clipPath(path);
  }
  return currentAlpha * _alpha;
}

std::unique_ptr<LayerStyleSource> Layer::getLayerStyleSource(const DrawArgs& args,
                                                             const Matrix& matrix,
                                                             bool excludeContour) {
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
      excludeContour
          ? false
          : std::any_of(_layerStyles.begin(), _layerStyles.end(), [](const auto& layerStyle) {
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
  PictureRecorder recorder = {};
  auto canvas = recorder.beginRecording();
  auto bounds = getBounds();
  bounds.scale(contentScale, contentScale);
  bounds.roundOut();
  canvas->clipRect(bounds);
  canvas->scale(contentScale, contentScale);
  auto localToGlobalMatrix = getGlobalMatrix();
  // If there are 3D transformations or projection transformations from the current layer node to
  // the root node, making it impossible to obtain an accurate background image and stretch it into
  // a rectangle, please use the getBoundsBackgroundImage interface to get the background image
  // corresponding to the minimum bounding rectangle of the current layer subtree after drawing.
  DEBUG_ASSERT(IsMatrix3DAffine(localToGlobalMatrix));
  auto affiineLocalToGlobalMatrix = GetMayLossyAffineMatrix(localToGlobalMatrix);
  Matrix affineGlobalToLocalMatrix = {};
  if (!affiineLocalToGlobalMatrix.invert(&affineGlobalToLocalMatrix)) {
    return nullptr;
  }
  canvas->concat(affineGlobalToLocalMatrix);
  drawBackgroundImage(args, *canvas);
  auto backgroundPicture = recorder.finishRecordingAsPicture();
  return ToImageWithOffset(std::move(backgroundPicture), offset, &bounds, args.dstColorSpace);
}

std::shared_ptr<Image> Layer::getBoundsBackgroundImage(const DrawArgs& args, float contentScale,
                                                       Point* offset) {
  if (args.drawMode == DrawMode::Background) {
    return nullptr;
  }

  PictureRecorder recorder = {};
  auto canvas = recorder.beginRecording();
  auto bounds = getBounds();
  bounds.scale(contentScale, contentScale);
  bounds.roundOut();
  canvas->clipRect(bounds);
  // Calculate the transformation matrix for drawing the background image within renderBounds to
  // bounds.
  auto matrix = Matrix::MakeTrans(-renderBounds.left, -renderBounds.top);
  matrix.postScale(bounds.width() * contentScale / renderBounds.width(),
                   bounds.height() * contentScale / renderBounds.height());
  matrix.postTranslate(bounds.left, bounds.top);
  canvas->setMatrix(matrix);

  drawBackgroundImage(args, *canvas);
  auto backgroundPicture = recorder.finishRecordingAsPicture();
  return ToImageWithOffset(std::move(backgroundPicture), offset, &bounds, args.dstColorSpace);
}

void Layer::drawBackgroundImage(const DrawArgs& args, Canvas& canvas) {
  if (args.blurBackground) {
    auto image = args.blurBackground->getBackgroundImage();
    canvas.concat(args.blurBackground->backgroundMatrix());
    canvas.drawImage(image);
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
    auto currentAlpha = drawBackgroundLayers(drawArgs, &canvas);
    // Draw the layer styles below the content, as they are part of the background.
    auto layerStyleSource = getLayerStyleSource(drawArgs, canvas.getMatrix());
    if (layerStyleSource) {
      drawLayerStyles(drawArgs, &canvas, currentAlpha, layerStyleSource.get(),
                      LayerStylePosition::Below);
    }
  }
}

void Layer::drawLayerStyles(const DrawArgs& args, Canvas* canvas, float alpha,
                            const LayerStyleSource* source, LayerStylePosition position) {
  std::unordered_set<LayerStyleExtraSourceType> extraSourceTypes = {
      LayerStyleExtraSourceType::None, LayerStyleExtraSourceType::Contour,
      LayerStyleExtraSourceType::Background};
  drawLayerStyles(args, canvas, alpha, source, position, extraSourceTypes);
}

void Layer::drawLayerStyles(const DrawArgs& args, Canvas* canvas, float alpha,
                            const LayerStyleSource* source, LayerStylePosition position,
                            const std::unordered_set<LayerStyleExtraSourceType>& extraSourceTypes) {
  DEBUG_ASSERT(source != nullptr && !FloatNearlyZero(source->contentScale));
  auto& contour = source->contour;
  auto contourOffset = source->contourOffset - source->contentOffset;
  auto backgroundCanvas = args.blurBackground ? args.blurBackground->getCanvas() : nullptr;
  auto matrix = Matrix::MakeScale(1.f / source->contentScale, 1.f / source->contentScale);
  matrix.preTranslate(source->contentOffset.x, source->contentOffset.y);
  AutoCanvasRestore autoRestore(canvas);
  canvas->concat(matrix);
  if (backgroundCanvas) {
    backgroundCanvas->save();
    backgroundCanvas->concat(matrix);
  }
  auto clipBounds =
      args.blurBackground ? GetClipBounds(args.blurBackground->getCanvas()) : std::nullopt;
  for (const auto& layerStyle : _layerStyles) {
    if (layerStyle->position() != position ||
        extraSourceTypes.find(layerStyle->extraSourceType()) == extraSourceTypes.end()) {
      continue;
    }
    PictureRecorder recorder = {};
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
        // If there are no 3D transformations or projection transformations from the current layer
        // node to the root node, the accurate background image can be obtained; otherwise, only the
        // background image corresponding to the minimum axis-aligned bounding rectangle after
        // projection can be obtained.
        auto background =
            IsMatrix3DAffine(getGlobalMatrix())
                ? getBackgroundImage(args, source->contentScale, &backgroundOffset)
                : getBoundsBackgroundImage(args, source->contentScale, &backgroundOffset);
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
    } else if (!clipBounds.has_value()) {
      canvas->drawPicture(picture);
      backgroundCanvas->drawPicture(picture);
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
    }
  }
  if (backgroundCanvas) {
    backgroundCanvas->restore();
  }
}

void Layer::drawBackgroundLayerStyles(const DrawArgs& args, Canvas* canvas, float alpha,
                                      const Matrix3D& transform) {
  auto styleSource = getLayerStyleSource(args, canvas->getMatrix(), true);
  if (styleSource == nullptr) {
    return;
  }

  // Apply an equivalent transformation to the content of StyleSource so that when used as a mask
  // for background processing, it can accurately coincide with the actual content rendering area,
  // ensuring that only the actual rendering region of the layer reveals the background.
  std::shared_ptr<ImageFilter> styleSourceFilter = nullptr;
  auto bounds = getBounds();
  auto transformedBounds = transform.mapRect(bounds);
  transformedBounds.roundOut();
  if (styleSource->content != nullptr) {
    // The object of styleSourceMatrix is Image. When drawing directly, Image doesn't care about
    // actual translation, so it can be ignored when constructing the matrix here.
    auto styleSourceAnchor = Point::Make(bounds.left, bounds.top);
    // StyleSourceMatrix acts on the content of styleSource, which is a subregion extracted from
    // within the Layer. For transformations based on the local coordinate system of this subregion,
    // anchor point adaptation is required for the matrix described based on the layer.
    auto styleSourceMatrix = anchorAdaptedMatrix(transform, styleSourceAnchor);
    styleSourceMatrix.postScale(bounds.width() / transformedBounds.width(),
                                bounds.height() / transformedBounds.height(), 1.0f);
    auto transform3DFilter = Transform3DFilter::Make(styleSourceMatrix);
    styleSourceFilter = transform3DFilter->getImageFilter(styleSource->contentScale);
    styleSource->content = styleSource->content->makeWithFilter(styleSourceFilter);
  }

  // When rendering offscreen, if the layer contains 3D transformations, since it's impossible to
  // accurately stretch and fill the background into a rectangular texture and then apply the 3D
  // matrix, we can only independently draw the background area. Then, by setting the effective
  // clipping area through the layer contour, we ensure that only the strictly projected background
  // area is correctly drawn.
  Matrix styleMatrix = {};
  Path styleClipPath = {};
  AutoCanvasRestore autoRestore(canvas);
  styleClipPath.addRect(bounds);
  styleClipPath.transform3D(transform);
  // StyleClipPath is the final clipping path based on the parent node, which must be called
  // before setting the matrix, otherwise it will be affected by the matrix.
  canvas->clipPath(styleClipPath);
  // The material sources for layer styles are all filled into the bounds. When drawing the
  // styles, it's necessary to calculate the transformation matrix that maps the bounds to the
  // actual drawing area transformedBounds.
  DEBUG_ASSERT(!FloatNearlyZero(bounds.width() * bounds.height()));
  styleMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  styleMatrix.postScale(transformedBounds.width() / bounds.width(),
                        transformedBounds.height() / bounds.height());
  styleMatrix.postTranslate(transformedBounds.left, transformedBounds.top);
  canvas->concat(styleMatrix);
  // When LayerStyle's ExtraSourceType is Background, its Position can only be Below, so there's no
  // need to handle the Position Above case here.
  drawLayerStyles(args, canvas, alpha, styleSource.get(), LayerStylePosition::Below,
                  {LayerStyleExtraSourceType::Background});
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
  // Ensure the 3D filter is not released during the entire function lifetime, otherwise the
  // transformer will have abnormal render bounds calculation
  auto filter3DVector = std::vector<std::shared_ptr<LayerFilter>>{};
  if (!_layerStyles.empty() || !_filters.empty() || !bitFields.matrix3DIsAffine) {
    if (transformer) {
      contentScale = transformer->getMaxScale();
    }
    // The filter and style should calculate bounds based on the original size. The externally
    // provided Transformer already contains matrix data, which will be applied to the computed size
    // at the end, including scaling, rotation, and other transformations.
    if (!bitFields.matrix3DIsAffine) {
      filter3DVector.push_back(Transform3DFilter::Make(_matrix3D));
      transformer =
          RegionTransformer::MakeFromFilters(filter3DVector, 1.0f, std::move(transformer));
    }
    transformer = RegionTransformer::MakeFromFilters(_filters, 1.0f, std::move(transformer));
    transformer = RegionTransformer::MakeFromStyles(_layerStyles, 1.0f, std::move(transformer));
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
    std::optional<Rect> clipRect = std::nullopt;
    // If the sublayer's Matrix contains 3D transformations or projection transformations, then its
    // Matrix is considered as an identity matrix here, and its actual position is left to the
    // sublayer to calculate through the 3D filter method.
    auto childAffineMatrix =
        IsMatrix3DAffine(childMatrix) ? GetMayLossyAffineMatrix(childMatrix) : Matrix::I();
    auto childTransformer = RegionTransformer::MakeFromMatrix(childAffineMatrix, transformer);
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
    // When marking the dirty region for 3D layer background styles, the influence range of layer
    // styles acts on the final projected Bounds, which has already applied 3D matrix transformation,
    // so the identity matrix can be directly used here.
    auto affineChildMatrix =
        IsMatrix3DAffine(childMatrix) ? GetMayLossyAffineMatrix(childMatrix) : Matrix::I();
    auto childTransformer = RegionTransformer::MakeFromMatrix(affineChildMatrix, transformer);
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
      layer->invalidateCache();
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
  if (!bitFields.dirtyDescendents) {
    return maxBackgroundOutset > 0;
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

std::shared_ptr<BackgroundContext> Layer::createBackgroundContext(
    Context* context, const Rect& drawRect, const Matrix& viewMatrix, bool fullLayer,
    std::shared_ptr<ColorSpace> colorSpace) const {
  if (maxBackgroundOutset <= 0.0f) {
    return nullptr;
  }
  if (fullLayer) {
    return BackgroundContext::Make(context, drawRect, 0, 0, viewMatrix, colorSpace);
  }
  auto scale = viewMatrix.getMaxScale();
  return BackgroundContext::Make(context, drawRect, maxBackgroundOutset * scale,
                                 minBackgroundOutset * scale, viewMatrix, colorSpace);
}

Matrix3D Layer::anchorAdaptedMatrix(const Matrix3D& matrix, const Point& anchor) const {
  // In the new coordinate system defined with anchor as the anchor point and origin, the reference
  // anchor point for the matrix transformation described by the layer's _matrix is located at
  // point (-anchor.x, -anchor.y). That is, to maintain the same transformation effect, first
  // translate the anchor point to the new coordinate origin, apply the original matrix, and then
  // reverse translate the anchor point.
  auto offsetMatrix = Matrix3D::MakeTranslate(anchor.x, anchor.y, 0);
  auto invOffsetMatrix = Matrix3D::MakeTranslate(-anchor.x, -anchor.y, 0);
  return invOffsetMatrix * matrix * offsetMatrix;
}

void Layer::invalidateCache() {
  rasterizedContent = nullptr;
  subTreeCache = nullptr;
}

}  // namespace tgfx
