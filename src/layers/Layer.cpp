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
#include <algorithm>
#include <atomic>
#include <cmath>
#include <queue>
#include "core/MCState.h"
#include "core/Matrix2D.h"
#include "core/filters/Transform3DImageFilter.h"
#include "core/images/TextureImage.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "layers/ContourContext.h"
#include "layers/DrawArgs.h"
#include "layers/MaskContext.h"
#include "layers/RegionTransformer.h"
#include "layers/RootLayer.h"
#include "layers/SubtreeCache.h"
#include "layers/contents/LayerContent.h"
#include "layers/filters/Transform3DFilter.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/ShapeLayer.h"

namespace tgfx {
// The minimum size (longest edge) for subtree cache. This prevents creating excessively small
// mipmap levels that would be inefficient to cache.
static constexpr int SUBTREE_CACHE_MIN_SIZE = 32;
static std::atomic_bool AllowsEdgeAntialiasing = true;
static std::atomic_bool AllowsGroupOpacity = false;

struct MaskData {
  Path clipPath = {};
  std::shared_ptr<MaskFilter> maskFilter = nullptr;
};

struct LayerStyleSource {
  float contentScale = 1.0f;
  std::shared_ptr<Image> content = nullptr;
  Point contentOffset = {};
  std::shared_ptr<Image> contour = nullptr;
  Point contourOffset = {};
};

// Determine if the 4*4 matrix contains only 2D affine transformations, i.e., no Z-axis related
// transformations or projection transformations
static inline bool IsMatrix3DAffine(const Matrix3D& matrix) {
  return FloatNearlyZero(matrix.getRowColumn(0, 2)) && FloatNearlyZero(matrix.getRowColumn(1, 2)) &&
         matrix.getRow(2) == Vec4(0, 0, 1, 0) && matrix.getRow(3) == Vec4(0, 0, 0, 1);
}

// When a 4x4 matrix does not contain Z-axis related transformations and projection transformations,
// this function returns an equivalent 2D affine transformation. Otherwise, the return value will
// lose information about Z-axis related transformations and projection transformations.
static inline Matrix GetMayLossyAffineMatrix(const Matrix3D& matrix) {
  auto affineMatrix = Matrix::I();
  affineMatrix.setAll(matrix.getRowColumn(0, 0), matrix.getRowColumn(0, 1),
                      matrix.getRowColumn(0, 3), matrix.getRowColumn(1, 0),
                      matrix.getRowColumn(1, 1), matrix.getRowColumn(1, 3));
  return affineMatrix;
}

/**
 * Applies a clip rect to the canvas, avoiding anti-aliasing artifacts that cause semi-transparent
 * edges when the scaled clip bounds are too small.
 * TODO: Adapt this function to clip offscreenCanvas in getContentImage.
 * @param maxBounds The maximum clip bounds before scaling.
 * @param clipBounds The desired clip bounds before scaling.
 */
// static inline void ApplyClip(Canvas& canvas, const Rect& maxBounds, const Rect& clipBounds,
//                              float contentScale) {
//   if (contentScale < 1.0f) {
//     auto scaledBounds = clipBounds;
//     scaledBounds.scale(contentScale, contentScale);
//     if (scaledBounds.width() < 10.f || scaledBounds.height() < 10.f) {
//       // When the actual clip bounds are too small, fractional values in the scaled clip region
//       // trigger AARectEffect's shader coverage clipping strategy, causing layers obtained from
//       // this clip region to become semi-transparent. This situation should be avoided.
//       DEBUG_ASSERT(!FloatNearlyZero(contentScale));
//       scaledBounds.roundOut();
//       scaledBounds.scale(1.0f / contentScale, 1.0f / contentScale);
//       scaledBounds.intersect(maxBounds);
//       canvas.clipRect(scaledBounds);
//       return;
//     }
//   }
//   canvas.clipRect(clipBounds);
// }

// Creates a RegionTransformer from a 3D transformation matrix.
// If the matrix is affine, uses MakeFromMatrix. Otherwise, creates a Transform3DFilter and uses
// MakeFromFilters. The filterHolder is used to ensure the filter is not released during the
// transformer's lifetime.
static inline std::shared_ptr<RegionTransformer> MakeTransformerFromTransform3D(
    const Matrix3D& transform3D, std::shared_ptr<RegionTransformer> outer,
    std::vector<std::shared_ptr<LayerFilter>>* filterHolder) {
  if (IsMatrix3DAffine(transform3D)) {
    return RegionTransformer::MakeFromMatrix(GetMayLossyAffineMatrix(transform3D),
                                             std::move(outer));
  }
  filterHolder->push_back(Transform3DFilter::Make(transform3D));
  return RegionTransformer::MakeFromFilters(*filterHolder, 1.0f, std::move(outer));
}

/**
 * Returns an adapted transformation matrix for a new coordinate system established at the specified
 * point.
 * The original matrix defines a transformation in a coordinate system with the origin (0, 0) as
 * the anchor point. When establishing a new coordinate system at an arbitrary point within this
 * space, this function computes the equivalent matrix that produces the same visual transformation
 * effect in the new coordinate system.
 * @param matrix3D The original transformation matrix defined with the origin (0, 0) as the anchor
 * point.
 * @param newOrigin The point at which to establish the new coordinate system as its origin.
 */
static inline Matrix3D OriginAdaptedMatrix3D(const Matrix3D& matrix3D, const Point& newOrigin) {
  auto offsetMatrix = Matrix3D::MakeTranslate(newOrigin.x, newOrigin.y, 0);
  auto invOffsetMatrix = Matrix3D::MakeTranslate(-newOrigin.x, -newOrigin.y, 0);
  return invOffsetMatrix * matrix3D * offsetMatrix;
}

/**
 * Draws a layer's snapshot image to the 3D rendering context.
 * @param layerTransform3D The transformation matrix to apply to the layer.
 * @param image The image to draw, which is a snapshot of a region within the layer after
 * transformation.
 * @param matrix The matrix that maps the scaled image back to the original region in the layer's
 * coordinate system.
 * @param alpha The alpha value to apply when drawing the image.
 */
static inline void DrawLayerImageTo3DRenderContext(const DrawArgs& args,
                                                   const Matrix3D* layerTransform3D,
                                                   const std::shared_ptr<Image>& image,
                                                   const Matrix& matrix, float alpha,
                                                   bool antiAlias) {
  if (layerTransform3D == nullptr || image == nullptr) {
    DEBUG_ASSERT(false);
    return;
  }

  auto sourceImageOrigin = Point::Make(matrix.getTranslateX(), matrix.getTranslateY());
  auto imageTransform = OriginAdaptedMatrix3D(*layerTransform3D, sourceImageOrigin);
  auto scaleX = matrix.getScaleX();
  auto scaleY = matrix.getScaleY();
  DEBUG_ASSERT(!FloatNearlyZero(scaleX));
  DEBUG_ASSERT(!FloatNearlyZero(scaleY));
  // Project first, then scale.
  if (!FloatNearlyEqual(scaleX, 1.0f) || !FloatNearlyEqual(scaleY, 1.0f)) {
    auto invScaleMatrix = Matrix3D::MakeScale(scaleX, scaleY, 1.0f);
    auto scaleMatrix = Matrix3D::MakeScale(1.0f / scaleX, 1.0f / scaleY, 1.0f);
    imageTransform = scaleMatrix * imageTransform * invScaleMatrix;
  }

  // imageTransform is in image coordinate space. Dividing by scale converts the translation to
  // DisplayList coordinate space, then subtracting renderRect offset converts to compositor space.
  imageTransform.postTranslate(
      matrix.getTranslateX() / scaleX - args.render3DContext->renderRect().left,
      matrix.getTranslateY() / scaleY - args.render3DContext->renderRect().top, 0);
  args.render3DContext->compositor()->addImage(image, imageTransform, alpha, antiAlias);
}

/**
 * Checks if any vertex of the rect is behind the camera after applying the 3D transformation.
 * A vertex is considered behind the camera when w <= 0, where w = 0 means the vertex is at the
 * camera plane (infinitely far), which is also treated as behind.
 * @param rect The rect in layer's local coordinate system.
 * @param transform3D The layer's transformation matrix, containing camera information.
 */
static inline bool IsTransformedLayerRectBehindCamera(const Rect& rect,
                                                      const Matrix3D& transform3D) {
  if (transform3D.mapHomogeneous(rect.left, rect.top, 0, 1).w <= 0 ||
      transform3D.mapHomogeneous(rect.left, rect.bottom, 0, 1).w <= 0 ||
      transform3D.mapHomogeneous(rect.right, rect.top, 0, 1).w <= 0 ||
      transform3D.mapHomogeneous(rect.right, rect.bottom, 0, 1).w <= 0) {
    return true;
  }
  return false;
}

static std::optional<Rect> MapClipBoundsToContent(const std::optional<Rect>& clipBounds,
                                                  const Matrix3D* transform3D) {
  if (transform3D != nullptr && clipBounds.has_value()) {
    auto filter = ImageFilter::Transform3D(*transform3D);
    if (filter) {
      return filter->filterBounds(*clipBounds, MapDirection::Reverse);
    }
  }
  return clipBounds;
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
    std::shared_ptr<ColorSpace> colorSpace = ColorSpace::SRGB(), bool roundOutBounds = true) {
  if (picture == nullptr) {
    return nullptr;
  }
  auto bounds = imageBounds ? *imageBounds : picture->getBounds();
  if (roundOutBounds) {
    // In off-screen rendering scenarios, the canvas matrix is applied to the picture, requiring
    // bounds to be rounded out to keep offsets integral and avoid redundant sampling.
    // During caching, the canvas matrix is not applied to the picture, so rounding is unnecessary.
    bounds.roundOut();
  }
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(std::move(picture), FloatCeilToInt(bounds.width()),
                               FloatCeilToInt(bounds.height()), &matrix, std::move(colorSpace));
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
  auto scaleBoundsSize = FloatRoundToInt(maxBoundsSize * contentScale);
  if (scaleBoundsSize > maxSize) {
    return scaleBoundsSize;
  }
  if (SUBTREE_CACHE_MIN_SIZE >= maxSize) {
    return maxSize;
  }
  auto targetSize = std::max(scaleBoundsSize, SUBTREE_CACHE_MIN_SIZE);
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

  auto result = matrix3D().mapPoint(Vec3(0, 0, 0));
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
    auto curPos = _matrix3D.mapPoint(Vec3(0, 0, 0));
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

void Layer::setTransformStyle(TransformStyle style) {
  if (_transformStyle == style) {
    return;
  }
  _transformStyle = style;
  // Changing the transform style alters the meaning of the layer's transform, so invalidate it.
  invalidateTransform();
}

void Layer::setVisible(bool value) {
  if (bitFields.visible == value) {
    return;
  }
  bitFields.visible = value;
  invalidateTransform();
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
  invalidateSubtree();
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
  invalidateSubtree();
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
  // Non-leaf nodes in a 3D rendering context layer tree must preserve the 3D state of their
  // sublayers, and cannot reuse the localBounds cache that has been flattened to the layer's local
  // coordinate system.
  if (computeTightBounds || bitFields.dirtyDescendents ||
      (_transformStyle == TransformStyle::Preserve3D && canExtend3DContext())) {
    return computeBounds(coordinateMatrix, computeTightBounds);
  }
  if (!localBounds) {
    localBounds = std::make_unique<Rect>(computeBounds(Matrix3D::I(), computeTightBounds));
  }
  auto result = coordinateMatrix.mapRect(*localBounds);
  if (!IsMatrix3DAffine(coordinateMatrix)) {
    // When the matrix is not affine, the layer will draw with a 3D filter, so the bounds should
    // be rounded out.
    result.roundOut();
  }
  return result;
}

static Rect ComputeContentBounds(const LayerContent& content, const Matrix3D& coordinateMatrix,
                                 bool applyMatrixAtEnd, bool computeTightBounds) {
  if (applyMatrixAtEnd) {
    if (computeTightBounds) {
      return content.getTightBounds(Matrix::I());
    }
    return content.getBounds();
  }
  if (computeTightBounds) {
    if (IsMatrix3DAffine(coordinateMatrix)) {
      return content.getTightBounds(GetMayLossyAffineMatrix(coordinateMatrix));
    }
    auto tightBounds = content.getTightBounds(Matrix::I());
    tightBounds = coordinateMatrix.mapRect(tightBounds);
    tightBounds.roundOut();
    return tightBounds;
  }
  return coordinateMatrix.mapRect(content.getBounds());
}

Rect Layer::computeBounds(const Matrix3D& coordinateMatrix, bool computeTightBounds) {
  bool canStart3DContext = _transformStyle == TransformStyle::Preserve3D && canExtend3DContext();
  // Layers that can start or extend a 3D rendering context do not support computeTightBounds.
  DEBUG_ASSERT(!canStart3DContext || !computeTightBounds);
  bool hasEffects = !_layerStyles.empty() || !_filters.empty();
  // When starting a 3D context, the matrix must be passed down to preserve 3D state.
  // Otherwise, when has effects, compute in local coordinates first, then apply matrix at end.
  bool applyMatrixAtEnd = !canStart3DContext && hasEffects;

  Rect bounds = {};
  if (auto content = getContent()) {
    bounds.join(
        ComputeContentBounds(*content, coordinateMatrix, applyMatrixAtEnd, computeTightBounds));
  }

  for (const auto& child : _children) {
    // Alpha does not need to be checked; alpha == 0 is still valid.
    if (!child->visible() || child->maskOwner) {
      continue;
    }
    auto childMatrix = child->getMatrixWithScrollRect();
    // Check if the child layer is within a 3D rendering context. If not, adapt the matrix to
    // ensure the z-component of layer rect vertex coordinates remains unchanged, so the child layer
    // is projected onto the current layer first, then the accumulated matrix transformation is
    // applied. If within, directly concatenate the child layer matrix to preserve its 3D state.
    if (_transformStyle == TransformStyle::Flat || !canExtend3DContext()) {
      childMatrix.setRow(2, {0, 0, 1, 0});
    }
    childMatrix.postConcat(applyMatrixAtEnd ? Matrix3D::I() : coordinateMatrix);
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

  if (hasEffects) {
    // Filters and styles disable 3D context ability, so this block is unreachable for 3D context.
    DEBUG_ASSERT(!canStart3DContext);
    auto layerBounds = bounds;
    for (auto& layerStyle : _layerStyles) {
      auto styleBounds = layerStyle->filterBounds(layerBounds, 1.0f);
      bounds.join(styleBounds);
    }
    for (auto& filter : _filters) {
      bounds = filter->filterBounds(bounds, 1.0f);
    }
  }

  if (applyMatrixAtEnd) {
    bounds = coordinateMatrix.mapRect(bounds);
    if (!IsMatrix3DAffine(coordinateMatrix)) {
      bounds.roundOut();
    }
  }
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
  auto result = globalMatrix.mapPoint({localPoint.x, localPoint.y, 0});
  return {result.x, result.y};
}

bool Layer::hitTestPoint(float x, float y, bool shapeHitTest) {
  if (auto content = getContent()) {
    Point localPoint = globalToLocal(Point::Make(x, y));
    if (shapeHitTest) {
      if (content->hitTestPoint(localPoint.x, localPoint.y)) {
        return true;
      }
    } else {
      if (content->getBounds().contains(localPoint.x, localPoint.y)) {
        return true;
      }
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
  auto blurCanvas = args.blurBackground ? args.blurBackground->getCanvas() : nullptr;
  AutoCanvasRestore autoRestore(canvas);
  AutoCanvasRestore blurAutoRestore(blurCanvas);

  // Check if the current layer needs to start a 3D context. Since Layer::draw is called directly
  // without going through a parent layer's drawChildren, 3D context handling must be done here.
  if (_transformStyle == TransformStyle::Preserve3D && canExtend3DContext()) {
    drawByStarting3DContext(args, canvas, alpha);
  } else {
    drawLayer(args, canvas, alpha, blendMode, nullptr);
  }
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
  invalidateSubtree();
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
    // Check if the child layer is within a 3D rendering context. If not, adapt the matrix to
    // ensure the z-component of layer rect vertex coordinates remains unchanged, so the child layer
    // is projected onto the current layer first, then the accumulated matrix transformation is
    // applied. If within, directly concatenate the child layer matrix to preserve its 3D state.
    if (layer->_transformStyle == TransformStyle::Flat || !layer->canExtend3DContext()) {
      matrix.setRow(2, {0, 0, 1, 0});
    }
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
  std::shared_ptr<MaskFilter> maskFilter = nullptr;
  if (!prepareMask(args, canvas, &maskFilter)) {
    return;
  }
  bool needsMaskFilter = maskFilter != nullptr;
  if (canUseSubtreeCache(args, blendMode, transform3D) &&
      drawWithSubtreeCache(args, canvas, alpha, blendMode, transform3D, maskFilter)) {
    return;
  }

  bool needsOffscreen = false;
  // For non-leaf layers in a 3D rendering context layer tree, direct rendering is always used.
  // Offscreen constraints (filters, styles, masks) are already checked by the parent layer at call
  // site. Group opacity, blend mode, and passThroughBackground have lower priority than enabling
  // 3D rendering context and are ignored during drawing.
  if (_transformStyle == TransformStyle::Flat || !canExtend3DContext()) {
    needsOffscreen = blendMode != BlendMode::SrcOver || !bitFields.passThroughBackground ||
                     (alpha < 1.0f && bitFields.allowsGroupOpacity) ||
                     (!_filters.empty() && !args.excludeEffects) || needsMaskFilter ||
                     transform3D != nullptr;
  }
  if (needsOffscreen) {
    // Content and children are rendered together in an offscreen buffer.
    drawOffscreen(args, canvas, alpha, blendMode, transform3D, maskFilter);
  } else {
    // Content and children are rendered independently.
    drawDirectly(args, canvas, alpha, transform3D);
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

bool Layer::prepareMask(const DrawArgs& args, Canvas* canvas,
                        std::shared_ptr<MaskFilter>* maskFilter) {
  if (!hasValidMask()) {
    return true;
  }
  auto clipBounds = GetClipBounds(args.blurBackground ? args.blurBackground->getCanvas() : canvas);
  auto contentScale = canvas->getMatrix().getMaxScale();
  auto maskData = getMaskData(args, contentScale, clipBounds);
  if (maskData.maskFilter == nullptr) {
    if (maskData.clipPath.isEmpty() && !maskData.clipPath.isInverseFillType()) {
      return false;
    }
    canvas->clipPath(maskData.clipPath);
    auto blurCanvas = args.blurBackground ? args.blurBackground->getCanvas() : nullptr;
    if (blurCanvas) {
      blurCanvas->clipPath(maskData.clipPath);
    }
    return true;
  }
  if (maskFilter) {
    *maskFilter = maskData.maskFilter;
  }
  return true;
}

MaskData Layer::getMaskData(const DrawArgs& args, float scale,
                            const std::optional<Rect>& layerClipBounds) {
  DEBUG_ASSERT(_mask != nullptr);
  auto maskType = static_cast<LayerMaskType>(bitFields.maskType);
  auto maskArgs = args;
  maskArgs.drawMode = maskType != LayerMaskType::Contour ? DrawMode::Normal : DrawMode::Contour;
  maskArgs.excludeEffects |= maskType == LayerMaskType::Contour;
  maskArgs.blurBackground = nullptr;
  // Mask content should be rendered to a regular texture, not to 3D compositor.
  maskArgs.render3DContext = nullptr;

  auto relativeMatrix = _mask->getRelativeMatrix(this);
  auto isMatrixAffine = IsMatrix3DAffine(relativeMatrix);
  auto affineRelativeMatrix =
      isMatrixAffine ? GetMayLossyAffineMatrix(relativeMatrix) : Matrix::I();

  auto maskPicture = RecordPicture(maskArgs.drawMode, scale, [&](Canvas* canvas) {
    if (layerClipBounds.has_value()) {
      auto scaledClipBounds = *layerClipBounds;
      scaledClipBounds.scale(scale, scale);
      scaledClipBounds.roundOut();
      scaledClipBounds.scale(1.0f / scale, 1.0f / scale);
      canvas->clipRect(scaledClipBounds);
    }
    canvas->concat(affineRelativeMatrix);
    _mask->drawLayer(maskArgs, canvas, _mask->_alpha, BlendMode::SrcOver);
  });
  if (maskPicture == nullptr) {
    return {};
  }

  // TODO:
  // The mask path gets clipped by layerClipBounds, causing incorrect masking in some tiles.
  // Temporarily disabled until the issue is fixed.
  // if (isMatrixAffine && maskType != LayerMaskType::Luminance) {
  //   Path maskPath = {};
  //   if (MaskContext::GetMaskPath(maskPicture, &maskPath)) {
  //     maskPath.transform(Matrix::MakeScale(1.0f / scale, 1.0f / scale));
  //     return {std::move(maskPath), nullptr};
  //   }
  // }

  Point maskImageOffset = {};
  auto maskContentImage =
      ToImageWithOffset(std::move(maskPicture), &maskImageOffset, nullptr, args.dstColorSpace);
  if (maskContentImage == nullptr) {
    return {};
  }
  if (maskType == LayerMaskType::Luminance) {
    maskContentImage =
        maskContentImage->makeWithFilter(ImageFilter::ColorFilter(ColorFilter::Luma()));
  }
  if (!isMatrixAffine) {
    maskContentImage = maskContentImage->makeWithFilter(ImageFilter::Transform3D(relativeMatrix));
  }
  auto maskMatrix = Matrix::MakeScale(1.0f / scale, 1.0f / scale);
  maskMatrix.preTranslate(maskImageOffset.x, maskImageOffset.y);

  auto shader = Shader::MakeImageShader(maskContentImage, TileMode::Decal, TileMode::Decal);
  if (shader) {
    shader = shader->makeWithMatrix(maskMatrix);
  }
  return {{}, MaskFilter::MakeShader(shader)};
}

std::shared_ptr<Image> Layer::getContentImage(const DrawArgs& contentArgs,
                                              const Matrix& contentMatrix,
                                              const std::optional<Rect>& clipBounds,
                                              Matrix* imageMatrix) {
  DEBUG_ASSERT(imageMatrix);
  auto inputBounds = computeContentBounds(clipBounds, contentArgs.excludeEffects);
  if (!inputBounds.has_value()) {
    return nullptr;
  }

  auto imageFilter =
      contentArgs.excludeEffects ? nullptr : getImageFilter(contentMatrix.getMaxScale());

  if (!imageFilter) {
    PictureRecorder recorder = {};
    auto offscreenCanvas = recorder.beginRecording();
    auto mappedBounds = contentMatrix.mapRect(*inputBounds);
    mappedBounds.roundOut();
    offscreenCanvas->clipRect(mappedBounds);
    offscreenCanvas->setMatrix(contentMatrix);
    drawDirectly(contentArgs, offscreenCanvas, 1.0f);
    Point offset = {};
    auto finalImage = ToImageWithOffset(recorder.finishRecordingAsPicture(), &offset, nullptr,
                                        contentArgs.dstColorSpace);
    if (!finalImage) {
      return nullptr;
    }
    contentMatrix.invert(imageMatrix);
    imageMatrix->preTranslate(offset.x, offset.y);
    return finalImage;
  }

  auto contentScale = contentMatrix.getMaxScale();
  PictureRecorder recorder = {};
  auto offscreenCanvas = recorder.beginRecording();
  auto mappedBounds = *inputBounds;
  mappedBounds.scale(contentScale, contentScale);
  mappedBounds.roundOut();
  offscreenCanvas->clipRect(mappedBounds);
  offscreenCanvas->scale(contentScale, contentScale);
  drawDirectly(contentArgs, offscreenCanvas, 1.0f);
  Point offset = {};
  auto finalImage = ToImageWithOffset(recorder.finishRecordingAsPicture(), &offset, nullptr,
                                      contentArgs.dstColorSpace);
  if (!finalImage) {
    return nullptr;
  }
  imageMatrix->setScale(1.0f / contentScale, 1.0f / contentScale);
  imageMatrix->preTranslate(offset.x, offset.y);

  std::optional<Rect> filterClipBounds = std::nullopt;
  if (clipBounds.has_value()) {
    auto invertMatrix = Matrix::I();
    imageMatrix->invert(&invertMatrix);
    filterClipBounds = invertMatrix.mapRect(*clipBounds);
    filterClipBounds->roundOut();
  }
  Point filterOffset = {};
  finalImage = finalImage->makeWithFilter(
      imageFilter, &filterOffset, filterClipBounds.has_value() ? &*filterClipBounds : nullptr);
  imageMatrix->preTranslate(filterOffset.x, filterOffset.y);
  return finalImage;
}

std::shared_ptr<Image> Layer::getPassThroughContentImage(const DrawArgs& args, Canvas* canvas,
                                                         const std::optional<Rect>& clipBounds,
                                                         Matrix* imageMatrix) {
  DEBUG_ASSERT(imageMatrix);
  DEBUG_ASSERT(args.context);
  auto surface = canvas->getSurface();
  DEBUG_ASSERT(surface);
  auto passThroughImage = surface->makeImageSnapshot();
  DEBUG_ASSERT(passThroughImage);
  auto passThroughImageMatrix = canvas->getMatrix();

  auto inputBounds = computeContentBounds(clipBounds, true);
  if (!inputBounds.has_value()) {
    return nullptr;
  }

  auto context = args.context;
  auto surfaceRect = passThroughImageMatrix.mapRect(*inputBounds);
  surfaceRect.roundOut();
  surfaceRect.intersect(Rect::MakeWH(passThroughImage->width(), passThroughImage->height()));
  auto offscreenSurface =
      Surface::Make(context, static_cast<int>(surfaceRect.width()),
                    static_cast<int>(surfaceRect.height()), false, 1, false, 0, args.dstColorSpace);
  if (!offscreenSurface) {
    return nullptr;
  }
  auto offscreenCanvas = offscreenSurface->getCanvas();
  offscreenCanvas->translate(-surfaceRect.left, -surfaceRect.top);
  offscreenCanvas->drawImage(passThroughImage);
  offscreenCanvas->concat(passThroughImageMatrix);
  drawDirectly(args, offscreenCanvas, 1.0f);
  auto finalImage = offscreenSurface->makeImageSnapshot();
  offscreenCanvas->getMatrix().invert(imageMatrix);
  return finalImage;
}

bool Layer::shouldPassThroughBackground(BlendMode blendMode, const Matrix3D* transform3D) const {
  // Pass-through background is only possible when:
  // 1. The feature is enabled
  if (!bitFields.passThroughBackground) {
    return false;
  }

  // 2. Subtree has blend modes that need background information
  if (!bitFields.hasBlendMode) {
    return false;
  }

  // 3. No offscreen rendering is required
  // (Offscreen rendering happens when: non-SrcOver blend mode OR has filters OR has 3D transform)
  bool needsOffscreen =
      blendMode != BlendMode::SrcOver || !_filters.empty() || transform3D != nullptr;
  if (needsOffscreen) {
    return false;
  }

  // 4. Layer does not start or extend a 3D context
  // (When starting or extending a 3D context, child layers need to maintain independent 3D states,
  // so pass-through background is not supported)
  if (_transformStyle == TransformStyle::Preserve3D && canExtend3DContext()) {
    return false;
  }

  return true;
}

bool Layer::canUseSubtreeCache(const DrawArgs& args, BlendMode blendMode,
                               const Matrix3D* transform3D) {
  // If the layer can start or extend a 3D rendering context, child layers need to maintain
  // independent 3D states, so subtree caching is not supported.
  if (_transformStyle == TransformStyle::Preserve3D && canExtend3DContext()) {
    return false;
  }
  // The cache stores Normal mode content. Since layers with BackgroundStyle are excluded from
  // caching, the cached content can also be used for Background mode drawing.
  if (args.excludeEffects || args.drawMode == DrawMode::Contour) {
    return false;
  }
  if (args.subtreeCacheMaxSize <= 0) {
    return false;
  }
  if (subtreeCache) {
    // Recreate if maxSize has changed
    if (subtreeCache->maxSize() != args.subtreeCacheMaxSize) {
      subtreeCache = std::make_unique<SubtreeCache>(args.subtreeCacheMaxSize);
    }
    return true;
  }
  if (shouldPassThroughBackground(blendMode, transform3D) || hasBackgroundStyle()) {
    return false;
  }
  if (!bitFields.staticSubtree) {
    // Skip caching on the first render to avoid caching content that is only displayed once.
    // The cache is created on the second render when the layer is confirmed to be reused.
    return false;
  }
  // Skip caching for leaf nodes with basic shapes (Rect, RRect) that have no filters or layer
  // styles, as they can be efficiently merged during rendering.
  if (_children.empty() && _layerStyles.empty() && _filters.empty()) {
    auto content = getContent();
    if (content == nullptr) {
      return false;
    }
    auto contentType = Types::Get(content);
    if (contentType == Types::LayerContentType::Rect ||
        contentType == Types::LayerContentType::RRect) {
      return false;
    }
  }
  subtreeCache = std::make_unique<SubtreeCache>(args.subtreeCacheMaxSize);
  return true;
}

std::shared_ptr<Image> Layer::createSubtreeCacheImage(const DrawArgs& args, float contentScale,
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
  // Cache content should be rendered to a regular texture, not to 3D compositor.
  drawArgs.render3DContext = nullptr;

  auto pictureBounds = layerBounds;
  pictureBounds.scale(contentScale, contentScale);
  auto filterBounds = pictureBounds;
  auto filter = getImageFilter(contentScale);
  if (filter) {
    auto reverseBounds = filter->filterBounds(pictureBounds, MapDirection::Reverse);
    pictureBounds.intersect(reverseBounds);
  }
  auto picture = RecordPicture(drawArgs.drawMode, contentScale,
                               [&](Canvas* canvas) { drawDirectly(drawArgs, canvas, 1.0f); });
  if (!picture) {
    return nullptr;
  }

  Point offset = {};
  auto image =
      ToImageWithOffset(std::move(picture), &offset, &pictureBounds, args.dstColorSpace, false);
  if (image == nullptr) {
    return nullptr;
  }

  if (filter) {
    filterBounds.offset(-offset.x, -offset.y);
    Point filterOffset = {};
    image = image->makeWithFilter(std::move(filter), &filterOffset, &filterBounds);
    offset += filterOffset;
  }

  drawingMatrix->setScale(1.0f / contentScale, 1.0f / contentScale);
  drawingMatrix->preTranslate(offset.x, offset.y);

  return image->makeTextureImage(args.context);
}

SubtreeCache* Layer::getValidSubtreeCache(const DrawArgs& args, int longEdge,
                                          const Rect& layerBounds) {
  if (subtreeCache->hasCache(args.context, longEdge)) {
    return subtreeCache.get();
  }
  if (args.renderFlags & RenderFlags::DisableCache || longEdge > args.subtreeCacheMaxSize) {
    return nullptr;
  }
  auto maxBoundsSize = std::max(layerBounds.width(), layerBounds.height());
  if (FloatNearlyZero(maxBoundsSize)) {
    return nullptr;
  }
  auto cacheScale = static_cast<float>(longEdge) / maxBoundsSize;
  auto imageMatrix = Matrix::I();
  auto image = createSubtreeCacheImage(args, cacheScale, layerBounds, &imageMatrix);
  if (image == nullptr) {
    return nullptr;
  }
  auto textureProxy = std::static_pointer_cast<TextureImage>(image)->getTextureProxy();
  subtreeCache->addCache(args.context, longEdge, textureProxy, imageMatrix, args.dstColorSpace);
  return subtreeCache.get();
}

bool Layer::drawWithSubtreeCache(const DrawArgs& args, Canvas* canvas, float alpha,
                                 BlendMode blendMode, const Matrix3D* transform3D,
                                 const std::shared_ptr<MaskFilter>& maskFilter) {
  // Non-leaf nodes in 3D rendering context layer tree have caching disabled.
  DEBUG_ASSERT(_transformStyle == TransformStyle::Flat || !canExtend3DContext());
  auto layerBounds = getBounds();
  auto contentScale = canvas->getMatrix().getMaxScale();
  auto longEdge = GetMipmapCacheLongEdge(args.subtreeCacheMaxSize, contentScale, layerBounds);
  auto cache = getValidSubtreeCache(args, longEdge, layerBounds);
  if (cache == nullptr) {
    return false;
  }
  Paint paint = {};
  if (maskFilter) {
    paint.setMaskFilter(maskFilter);
  }
  paint.setAntiAlias(bitFields.allowsEdgeAntialiasing);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  cache->draw(args.context, longEdge, canvas, paint, transform3D);
  if (args.blurBackground &&
      args.styleSourceTypes.count(LayerStyleExtraSourceType::Background) > 0) {
    cache->draw(args.context, longEdge, args.blurBackground->getCanvas(), paint, transform3D);
  }
  return true;
}

void Layer::drawOffscreen(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                          const Matrix3D* transform3D,
                          const std::shared_ptr<MaskFilter>& maskFilter) {
  // Non-leaf layers in a 3D rendering context layer tree never require offscreen rendering.
  DEBUG_ASSERT(args.render3DContext == nullptr);
  DEBUG_ASSERT(_transformStyle == TransformStyle::Flat || !canExtend3DContext());
  if (transform3D != nullptr &&
      args.styleSourceTypes.count(LayerStyleExtraSourceType::Background) > 0) {
    drawBackgroundLayerStyles(args, canvas, alpha, *transform3D);
  }

  auto imageMatrix = Matrix::I();
  std::shared_ptr<Image> image = nullptr;
  auto clipBoundsCanvas =
      args.blurBackground && !args.clipContentByCanvas ? args.blurBackground->getCanvas() : canvas;
  auto clipBounds = GetClipBounds(clipBoundsCanvas);
  auto contentArgs = args;

  if (shouldPassThroughBackground(blendMode, transform3D) && canvas->getSurface()) {
    // In pass-through mode, the image drawn to canvas contains the blended background, while
    // the image drawn to backgroundCanvas should be the layer content without background blending.
    // Use canvas's clipBounds instead of blurBackground's for pass-through rendering.
    auto canvasClipBounds = args.blurBackground ? GetClipBounds(canvas) : clipBounds;
    auto contentClipBounds = MapClipBoundsToContent(canvasClipBounds, transform3D);
    contentArgs.blurBackground =
        args.blurBackground ? args.blurBackground->createSubContext(renderBounds, true) : nullptr;
    image = getPassThroughContentImage(args, canvas, contentClipBounds, &imageMatrix);
  } else {
    auto contentClipBounds = MapClipBoundsToContent(clipBounds, transform3D);
    contentArgs.blurBackground = args.blurBackground && hasBackgroundStyle()
                                     ? args.blurBackground->createSubContext(renderBounds, true)
                                     : nullptr;
    image = getContentImage(contentArgs, canvas->getMatrix(), contentClipBounds, &imageMatrix);
  }

  auto invertImageMatrix = Matrix::I();
  if (image == nullptr || !imageMatrix.invert(&invertImageMatrix)) {
    return;
  }

  image = MakeImageWithTransform(image, transform3D, &imageMatrix);

  if (args.blurBackground && !contentArgs.blurBackground) {
    auto canvasScale = canvas->getMatrix().getMaxScale();
    auto backgroundScale = args.blurBackground->getCanvas()->getMatrix().getMaxScale();
    // When canvasScale equals backgroundScale, most pixels at the same resolution are valuable,
    // so rasterization is meaningful. Otherwise, skip rasterization to avoid generating oversized
    // textures.
    if (FloatNearlyEqual(canvasScale, backgroundScale)) {
      image = image->makeRasterized();
    }
  }

  Paint paint = {};
  paint.setAntiAlias(false);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);

  if (maskFilter) {
    paint.setMaskFilter(maskFilter->makeWithMatrix(invertImageMatrix));
  }

  canvas->concat(imageMatrix);
  auto filterMode =
      !args.excludeEffects && !_filters.empty() ? FilterMode::Linear : FilterMode::Nearest;
  auto sampling = SamplingOptions{filterMode, MipmapMode::None};
  canvas->drawImage(image, 0.f, 0.f, sampling, &paint);
  if (args.blurBackground) {
    if (contentArgs.blurBackground) {
      auto contentScale = canvas->getMatrix().getMaxScale();
      auto filter = getImageFilter(contentScale);
      paint.setImageFilter(filter);
      paint.setMaskFilter(maskFilter);
      contentArgs.blurBackground->drawToParent(paint);
    } else {
      auto backgroundCanvas = args.blurBackground->getCanvas();
      backgroundCanvas->concat(imageMatrix);
      backgroundCanvas->drawImage(image, 0.f, 0.f, sampling, &paint);
    }
  }
}

std::optional<Rect> Layer::computeContentBounds(const std::optional<Rect>& clipBounds,
                                                bool excludeEffects) {
  auto inputBounds = getBounds();
  if (clipBounds.has_value()) {
    auto mappedClipBounds = *clipBounds;
    if (!excludeEffects) {
      auto filter = getImageFilter(1.0f);
      if (filter) {
        mappedClipBounds = filter->filterBounds(mappedClipBounds, MapDirection::Reverse);
      }
    }
    if (!inputBounds.intersect(mappedClipBounds)) {
      return std::nullopt;
    }
  }
  inputBounds.roundOut();
  return inputBounds;
}

void Layer::drawDirectly(const DrawArgs& args, Canvas* canvas, float alpha,
                         const Matrix3D* transform3D) {
  auto layerStyleSource = getLayerStyleSource(args, canvas->getMatrix());
  drawContents(args, canvas, alpha, layerStyleSource.get(), nullptr, transform3D);
}

void Layer::drawContents(const DrawArgs& args, Canvas* canvas, float alpha,
                         const LayerStyleSource* layerStyleSource, const Layer* stopChild,
                         const Matrix3D* transform3D) {
  if (layerStyleSource) {
    drawLayerStyles(args, canvas, alpha, layerStyleSource, LayerStylePosition::Below);
  }
  auto content = getContent();
  auto drawBackground = args.styleSourceTypes.count(LayerStyleExtraSourceType::Background) > 0;
  bool hasForeground = false;
  if (content) {
    if (args.drawMode == DrawMode::Contour) {
      content->drawContour(canvas, bitFields.allowsEdgeAntialiasing);
    } else {
      hasForeground = content->drawDefault(canvas, alpha, bitFields.allowsEdgeAntialiasing);
      if (args.blurBackground && bitFields.matrix3DIsAffine && drawBackground) {
        content->drawDefault(args.blurBackground->getCanvas(), alpha,
                             bitFields.allowsEdgeAntialiasing);
      }
    }
  }
  if (!drawChildren(args, canvas, alpha, stopChild, transform3D)) {
    return;
  }
  if (layerStyleSource) {
    drawLayerStyles(args, canvas, alpha, layerStyleSource, LayerStylePosition::Above);
  }
  if (hasForeground) {
    content->drawForeground(canvas, alpha, bitFields.allowsEdgeAntialiasing);
    if (args.blurBackground && bitFields.matrix3DIsAffine && drawBackground) {
      content->drawForeground(args.blurBackground->getCanvas(), alpha,
                              bitFields.allowsEdgeAntialiasing);
    }
  }
}

bool Layer::drawChildren(const DrawArgs& args, Canvas* canvas, float alpha, const Layer* stopChild,
                         const Matrix3D* transform3D) {
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

  // TODO: Support background styles for subsequent layers of 3D layers.
  // 3D layer matrices are not written to the background canvas, so child layers cannot obtain
  // correct background content. Background styles are temporarily disabled for the 3D layer's
  // subtree (excluding the 3D layer itself).
  bool skipChildBackground =
      args.styleSourceTypes.count(LayerStyleExtraSourceType::Background) == 0 ||
      !bitFields.matrix3DIsAffine;
  // TODO: Support calculating reasonable clipping regions when obtaining content image.
  // Since 3D layer matrices are not written to the background canvas, child layers cannot compute
  // valid content clipping regions from it. Use the offscreen canvas clipping regions instead.
  bool clipChildContentByCanvas = args.clipContentByCanvas || !bitFields.matrix3DIsAffine;

  for (size_t i = 0; i < _children.size(); ++i) {
    auto& child = _children[i];
    if (child.get() == stopChild) {
      return false;
    }
    if (child->maskOwner || !child->visible() || child->_alpha <= 0) {
      continue;
    }
    auto childArgs = args;
    if (skipChildBackground) {
      childArgs.styleSourceTypes.erase(LayerStyleExtraSourceType::Background);
    }
    childArgs.clipContentByCanvas = clipChildContentByCanvas;
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
    AutoCanvasRestore autoRestoreBg(backgroundCanvas);
    auto childMatrix = child->getMatrixWithScrollRect();
    // If the sublayer's Matrix contains 3D transformations or projection transformations, then
    // treat its Matrix as an identity matrix here, and let the sublayer handle its actual position
    // through 3D filter methods,
    const bool isChildMatrixAffine = IsMatrix3DAffine(childMatrix);
    auto childAffineMatrix =
        (isChildMatrixAffine && (_transformStyle == TransformStyle::Flat || !canExtend3DContext()))
            ? GetMayLossyAffineMatrix(childMatrix)
            : Matrix::I();
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
      backgroundCanvas->concat(childAffineMatrix);
      clipChildScrollRectHandler(*backgroundCanvas);
    }

    if (childArgs.render3DContext != nullptr) {
      if (transform3D == nullptr) {
        DEBUG_ASSERT(false);
        continue;
      }
      // For layers in a 3D rendering context, draw the child layer to an offscreen canvas and
      // composite the result to the 3D compositor.
      auto childTransform = childMatrix;
      childTransform.postConcat(*transform3D);
      if (IsTransformedLayerRectBehindCamera(child->getBounds(), childTransform)) {
        continue;
      }
      child->drawIn3DContext(childArgs, canvas, child->_alpha * alpha, childTransform);
    } else if (child->_transformStyle == TransformStyle::Preserve3D &&
               child->canExtend3DContext()) {
      DEBUG_ASSERT(transform3D == nullptr);
      if (IsTransformedLayerRectBehindCamera(child->getBounds(), childMatrix)) {
        continue;
      }
      // Start a 3D rendering context.
      child->drawByStarting3DContext(childArgs, canvas, child->_alpha * alpha);
    } else {
      // Draw elements outside the 3D rendering context.
      DEBUG_ASSERT(transform3D == nullptr);
      auto childTransform = isChildMatrixAffine ? nullptr : &childMatrix;
      child->drawLayer(childArgs, canvas, child->_alpha * alpha,
                       static_cast<BlendMode>(child->bitFields.blendMode), childTransform);
    }

    if (backgroundCanvas) {
      backgroundCanvas->restore();
    }
  }
  return true;
}

void Layer::drawByStarting3DContext(const DrawArgs& args, Canvas* canvas, float alpha) {
  DEBUG_ASSERT(args.render3DContext == nullptr);
  if (args.renderRect == nullptr) {
    DEBUG_ASSERT(false);
    return;
  }
  if (args.context == nullptr) {
    DEBUG_ASSERT(false);
    return;
  }

  // The processing area of the compositor is consistent with the actual effective drawing area.
  auto clipBoundsCanvas =
      args.blurBackground && !args.clipContentByCanvas ? args.blurBackground->getCanvas() : canvas;
  // The clip bounds may be slightly larger than the dirty region.
  auto clipBounds = GetClipBounds(clipBoundsCanvas);
  if (!clipBounds.has_value()) {
    DEBUG_ASSERT(false);
    return;
  }
  auto validRenderRect = *clipBounds;
  validRenderRect.intersect(getBounds(_parent));
  // validRenderRect is in DisplayList coordinate system. Apply canvas scale to adapt to pixel
  // coordinates, avoiding excessive scaling when rendering to offscreen texture which would affect
  // the final visual quality.
  auto maxScale = canvas->getMatrix().getMaxScale();
  validRenderRect.scale(maxScale, maxScale);
  validRenderRect.roundOut();

  auto compositor = std::make_shared<Context3DCompositor>(
      *args.context, static_cast<int>(validRenderRect.width()),
      static_cast<int>(validRenderRect.height()));
  auto context3DArgs = args;
  context3DArgs.render3DContext = std::make_shared<Render3DContext>(compositor, validRenderRect);
  // Layers inside a 3D rendering context need to maintain independent 3D state. This means layers
  // drawn later may become the background, making it impossible to know the final background when
  // drawing each layer. Therefore, background styles are disabled.
  context3DArgs.styleSourceTypes = {LayerStyleExtraSourceType::None,
                                    LayerStyleExtraSourceType::Contour};
  drawIn3DContext(context3DArgs, canvas, alpha, getMatrixWithScrollRect());
  auto context3DImage = compositor->finish();

  // The final texture has been scaled proportionally during generation, so draw it at its actual
  // size on the canvas.
  auto contentScale = canvas->getMatrix().getMaxScale();
  DEBUG_ASSERT(!FloatNearlyZero(contentScale));
  AutoCanvasRestore autoRestore(canvas);
  auto imageMatrix = Matrix::MakeScale(1.0f / contentScale, 1.0f / contentScale);
  imageMatrix.preTranslate(validRenderRect.left, validRenderRect.top);
  canvas->concat(imageMatrix);
  canvas->drawImage(context3DImage);

  if (args.blurBackground) {
    Paint paint = {};
    paint.setAntiAlias(bitFields.allowsEdgeAntialiasing);
    auto backgroundCanvas = args.blurBackground->getCanvas();
    AutoCanvasRestore autoRestoreBg(backgroundCanvas);
    backgroundCanvas->concat(imageMatrix);
    backgroundCanvas->drawImage(context3DImage, &paint);
  }
}

void Layer::drawIn3DContext(const DrawArgs& args, Canvas* canvas, float alpha,
                            const Matrix3D& transform3D) {
  DEBUG_ASSERT(args.render3DContext != nullptr);
  auto contentScale = canvas->getMatrix().getMaxScale();
  if (FloatNearlyZero(contentScale)) {
    return;
  }

  PictureRecorder recorder = {};
  auto offscreenCanvas = recorder.beginRecording();
  offscreenCanvas->scale(contentScale, contentScale);

  if (transformStyle() == TransformStyle::Preserve3D && canExtend3DContext()) {
    drawLayer(args, offscreenCanvas, 1.0f, BlendMode::SrcOver, &transform3D);
  } else {
    // If the layer cannot extend the 3D rendering context, child layers do not need to create new
    // offscreen canvases via the context to collect content, so stop propagating the context.
    // The transform3D is not passed to drawLayer; the entire subtree rooted at this layer is
    // rendered offscreen and then drawn to the canvas with the matrix applied uniformly.
    auto drawArgs = args;
    drawArgs.render3DContext = nullptr;
    drawLayer(drawArgs, offscreenCanvas, 1.0f, BlendMode::SrcOver, nullptr);
  }

  Point offset = {};
  auto image =
      ToImageWithOffset(recorder.finishRecordingAsPicture(), &offset, nullptr, args.dstColorSpace);
  if (image == nullptr) {
    return;
  }

  // Calculate the image matrix that maps the scaled image back to the layer's coordinate system.
  auto imageMatrix = Matrix::I();
  imageMatrix.setScale(1.0f / contentScale, 1.0f / contentScale);
  imageMatrix.preTranslate(offset.x, offset.y);
  DrawLayerImageTo3DRenderContext(args, &transform3D, image, imageMatrix, alpha,
                                  bitFields.allowsEdgeAntialiasing);
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
  // Layer style source content should be rendered to a regular texture, not to 3D compositor.
  drawArgs.render3DContext = nullptr;
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
  matrix.postScale(bounds.width() / renderBounds.width(), bounds.height() / renderBounds.height());
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
  DEBUG_ASSERT(source != nullptr && !FloatNearlyZero(source->contentScale));
  auto& contour = source->contour;
  auto contourOffset = source->contourOffset - source->contentOffset;
  auto backgroundCanvas = args.blurBackground ? args.blurBackground->getCanvas() : nullptr;
  auto clipBounds =
      args.blurBackground ? GetClipBounds(args.blurBackground->getCanvas()) : std::nullopt;
  for (const auto& layerStyle : _layerStyles) {
    if (layerStyle->position() != position ||
        args.styleSourceTypes.count(layerStyle->extraSourceType()) == 0) {
      continue;
    }
    PictureRecorder recorder = {};
    auto pictureCanvas = recorder.beginRecording();
    if (clipBounds.has_value()) {
      pictureCanvas->clipRect(*clipBounds);
    }
    auto matrix = Matrix::MakeScale(1.f / source->contentScale, 1.f / source->contentScale);
    matrix.preTranslate(source->contentOffset.x, source->contentOffset.y);
    pictureCanvas->concat(matrix);
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
}

void Layer::drawBackgroundLayerStyles(const DrawArgs& args, Canvas* canvas, float alpha,
                                      const Matrix3D& transform3D) {
  auto styleSource = getLayerStyleSource(args, canvas->getMatrix(), true);
  if (styleSource == nullptr) {
    return;
  }

  // Apply an equivalent transformation to the content of StyleSource so that when used as a mask
  // for background processing, it can accurately coincide with the actual content rendering area,
  // ensuring that only the actual rendering region of the layer reveals the background.
  std::shared_ptr<ImageFilter> styleSourceFilter = nullptr;
  auto bounds = getBounds();
  auto transformedBounds = transform3D.mapRect(bounds);
  transformedBounds.roundOut();
  if (styleSource->content != nullptr) {
    // The object of styleSourceMatrix is Image. When drawing directly, Image doesn't care about
    // actual translation, so it can be ignored when constructing the matrix here.
    auto styleSourceAnchor = Point::Make(bounds.left, bounds.top);
    // StyleSourceMatrix acts on the content of styleSource, which is a subregion extracted from
    // within the Layer. For transformations based on the local coordinate system of this subregion,
    // anchor point adaptation is required for the matrix described based on the layer.
    auto styleSourceMatrix = OriginAdaptedMatrix3D(transform3D, styleSourceAnchor);
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
  styleClipPath.transform3D(transform3D);
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
  auto styleArgs = args;
  styleArgs.styleSourceTypes = {LayerStyleExtraSourceType::Background};
  drawLayerStyles(styleArgs, canvas, alpha, styleSource.get(), LayerStylePosition::Below);
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

void Layer::updateRenderBounds(std::shared_ptr<RegionTransformer> transformer,
                               const Matrix3D* transform3D, bool forceDirty) {
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
  auto baseTransformer = transformer;
  // Ensure the 3D filter is not released during the entire function lifetime.
  std::vector<std::shared_ptr<LayerFilter>> current3DFilterVector = {};
  if (transform3D != nullptr) {
    transformer = MakeTransformerFromTransform3D(*transform3D, std::move(transformer),
                                                 &current3DFilterVector);
  }
  if (!_layerStyles.empty() || !_filters.empty()) {
    // Filters and styles interrupt 3D rendering context, so non-root layers inside 3D rendering
    // context can ignore parent filters and styles when calculating dirty regions.
    DEBUG_ASSERT(_transformStyle == TransformStyle::Flat || !canExtend3DContext());
    if (transformer) {
      contentScale = transformer->getMaxScale();
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
    bool childIn3DContext =
        transform3D != nullptr ||
        (child->_transformStyle == TransformStyle::Preserve3D && child->canExtend3DContext());
    std::shared_ptr<RegionTransformer> childTransformer = nullptr;
    // Ensure the 3D filter is not released during the entire function lifetime.
    std::vector<std::shared_ptr<LayerFilter>> child3DFilterVector = {};
    if (childIn3DContext) {
      childTransformer = baseTransformer;
    } else {
      childTransformer =
          MakeTransformerFromTransform3D(childMatrix, transformer, &child3DFilterVector);
    }
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
    std::optional<Matrix3D> childTransform3D = std::nullopt;
    if (childIn3DContext) {
      childTransform3D = childMatrix;
      if (transform3D != nullptr) {
        childTransform3D->postConcat(*transform3D);
      }
    }
    child->updateRenderBounds(std::move(childTransformer),
                              childTransform3D.has_value() ? &*childTransform3D : nullptr,
                              childForceDirty);
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
  for (auto& style : _layerStyles) {
    if (style->extraSourceType() == LayerStyleExtraSourceType::Background) {
      _root->invalidateBackground(renderBounds, style.get(), contentScale);
    }
  }
  if (bitFields.hasBlendMode) {
    _root->invalidateBackground(renderBounds, nullptr, contentScale);
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

bool Layer::canExtend3DContext() const {
  return _filters.empty() && _layerStyles.empty() && !hasValidMask();
}

void Layer::invalidateSubtree() {
  bitFields.staticSubtree = false;
  subtreeCache = nullptr;
  localBounds = nullptr;
}

void Layer::updateStaticSubtreeFlags() {
  if (bitFields.staticSubtree) {
    return;
  }
  bitFields.staticSubtree = true;
  for (const auto& child : _children) {
    child->updateStaticSubtreeFlags();
  }
}

}  // namespace tgfx
