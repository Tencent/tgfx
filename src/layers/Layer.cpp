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
#include <unordered_map>
#include <unordered_set>
#include "compositing3d/Context3DCompositor.h"
#include "compositing3d/Layer3DContext.h"
#include "core/MCState.h"
#include "core/Matrix3DUtils.h"
#include "core/images/TextureImage.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "layers/DrawArgs.h"
#include "layers/MaskContext.h"
#include "layers/OpaqueContext.h"
#include "layers/RegionTransformer.h"
#include "layers/RootLayer.h"
#include "layers/SubtreeCache.h"
#include "layers/contents/LayerContent.h"
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
static const std::vector<LayerStyleExtraSourceType> StyleSourceTypesFor3DContext = {
    LayerStyleExtraSourceType::None, LayerStyleExtraSourceType::Contour};

static bool HasStyleSource(const std::vector<LayerStyleExtraSourceType>& types,
                           LayerStyleExtraSourceType type) {
  return std::find(types.begin(), types.end(), type) != types.end();
}

static void RemoveStyleSource(std::vector<LayerStyleExtraSourceType>& types,
                              LayerStyleExtraSourceType type) {
  types.erase(std::remove(types.begin(), types.end(), type), types.end());
}

// When canvasScale equals backgroundScale, most pixels at the same resolution are valuable,
// so rasterization is meaningful. Otherwise, skip rasterization to avoid generating oversized
// textures.
static bool ShouldRasterizeForBackground(Canvas* canvas,
                                         const std::shared_ptr<BackgroundContext>& blurBackground) {
  if (blurBackground == nullptr) {
    return false;
  }
  auto canvasScale = canvas->getMatrix().getMaxScale();
  auto backgroundScale = blurBackground->getCanvas()->getMatrix().getMaxScale();
  return FloatNearlyEqual(canvasScale, backgroundScale);
}

/**
 * Clips the canvas using the scroll rect. If the sublayer's Matrix contains 3D transformations or
 * projection transformations, because this matrix has been merged into the Canvas, it can be
 * directly completed through the clipping rectangle. Otherwise, the canvas will not contain this
 * matrix information, and clipping needs to be done by transforming the Path.
 */
static void ClipScrollRect(Canvas* canvas, const Rect* scrollRect, const Matrix3D* transform) {
  if (scrollRect == nullptr) {
    return;
  }

  if (transform != nullptr) {
    Path path;
    path.addRect(*scrollRect);
    path.transform3D(*transform);
    canvas->clipPath(path);
  } else {
    canvas->clipRect(*scrollRect);
  }
}

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

// Computes which layers need to be marked dirty during children reordering.
// Uses LIS (Longest Increasing Subsequence) algorithm to minimize dirty area
// by identifying layers that maintain their relative order.
static void ComputeDirtyNodesForReordering(const std::vector<Layer*>& retainedChildren,
                                           const std::vector<size_t>& newPositions,
                                           std::vector<Layer*>* nodesToMarkDirty) {
  size_t n = retainedChildren.size();
  if (n == 0) {
    return;
  }

  std::vector<size_t> lis;
  std::vector<size_t> predecessors(n, SIZE_MAX);
  std::vector<size_t> lisIndices;

  for (size_t i = 0; i < n; ++i) {
    size_t pos = newPositions[i];
    auto it = std::lower_bound(lis.begin(), lis.end(), pos);
    size_t insertPos = static_cast<size_t>(it - lis.begin());

    if (it == lis.end()) {
      lis.push_back(pos);
      lisIndices.push_back(i);
    } else {
      *it = pos;
      lisIndices[insertPos] = i;
    }

    if (insertPos > 0) {
      predecessors[i] = lisIndices[insertPos - 1];
    }
  }

  std::vector<bool> inLIS(n, false);
  if (!lisIndices.empty()) {
    size_t current = lisIndices.back();
    while (current != SIZE_MAX) {
      inLIS[current] = true;
      current = predecessors[current];
    }
  }

  for (size_t i = 0; i < n; ++i) {
    if (!inLIS[i]) {
      nodesToMarkDirty->push_back(retainedChildren[i]);
    }
  }
}

static std::shared_ptr<Picture> RecordPicture(float contentScale,
                                              const std::function<void(Canvas*)>& drawFunction) {
  if (drawFunction == nullptr) {
    return nullptr;
  }
  PictureRecorder recorder = {};
  auto canvas = recorder.beginRecording();
  canvas->scale(contentScale, contentScale);
  drawFunction(canvas);
  return recorder.finishRecordingAsPicture();
}

static std::shared_ptr<Picture> RecordOpaquePicture(
    float contentScale, const std::function<void(Canvas*, OpaqueContext*)>& drawFunction) {
  if (drawFunction == nullptr) {
    return nullptr;
  }
  OpaqueContext opaqueContext;
  auto canvas = opaqueContext.beginRecording();
  canvas->scale(contentScale, contentScale);
  drawFunction(canvas, &opaqueContext);
  return opaqueContext.finishRecordingAsPicture();
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

/**
 * Creates a 3D rendering context for compositing layers with 3D transformations.
 * @param bounds The bounding rectangle of the 3D context in DisplayList coordinates.
 */
static std::shared_ptr<Layer3DContext> Create3DContext(const DrawArgs& args, Canvas* canvas,
                                                       const Rect& bounds) {
  if (args.context == nullptr) {
    DEBUG_ASSERT(false);
    return nullptr;
  }

  // The processing area of the compositor is consistent with the actual effective drawing area.
  auto clipBoundsCanvas = args.blurBackground ? args.blurBackground->getCanvas() : canvas;

  auto validRenderRect = bounds;
  // The clip bounds may be slightly larger than the dirty region.
  auto clipBounds = GetClipBounds(clipBoundsCanvas);
  if (clipBounds.has_value() && !validRenderRect.intersect(*clipBounds)) {
    return nullptr;
  }
  // validRenderRect is in DisplayList coordinate system. Apply canvas scale to adapt to pixel
  // coordinates, avoiding excessive scaling when rendering to offscreen texture which would affect
  // the final visual quality.
  auto contentScale = canvas->getMatrix().getMaxScale();
  if (contentScale <= 0.0f) {
    return nullptr;
  }
  validRenderRect.scale(contentScale, contentScale);
  validRenderRect.roundOut();
  if (validRenderRect.isEmpty()) {
    return nullptr;
  }

  bool opaqueMode = args.opaqueContext != nullptr;
  return Layer3DContext::Make(opaqueMode, args.context, validRenderRect, contentScale,
                              args.dstColorSpace, args.blurBackground);
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
    DEBUG_ASSERT(filter != nullptr);
    filter->detachFromLayer(this);
  }
  for (const auto& layerStyle : _layerStyles) {
    DEBUG_ASSERT(layerStyle != nullptr);
    layerStyle->detachFromLayer(this);
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
  bitFields.matrix3DIsAffine = Matrix3DUtils::IsMatrix3DAffine(value);
  invalidateTransform();
}

void Layer::setPreserve3D(bool value) {
  if (_preserve3D == value) {
    return;
  }
  _preserve3D = value;
  // Changing preserve3D alters the meaning of the layer's transform, so invalidate it.
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

void Layer::setFilters(const std::vector<std::shared_ptr<LayerFilter>>& value) {
  if (_filters.size() == value.size() &&
      std::equal(_filters.begin(), _filters.end(), value.begin())) {
    return;
  }
  for (const auto& filter : _filters) {
    DEBUG_ASSERT(filter != nullptr);
    filter->detachFromLayer(this);
  }
  _filters.clear();
  for (const auto& filter : value) {
    if (filter == nullptr) {
      continue;
    }
    filter->attachToLayer(this);
    _filters.push_back(filter);
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

void Layer::setLayerStyles(const std::vector<std::shared_ptr<LayerStyle>>& value) {
  if (_layerStyles.size() == value.size() &&
      std::equal(_layerStyles.begin(), _layerStyles.end(), value.begin())) {
    return;
  }
  for (const auto& layerStyle : _layerStyles) {
    DEBUG_ASSERT(layerStyle != nullptr);
    layerStyle->detachFromLayer(this);
  }
  _layerStyles.clear();
  for (const auto& layerStyle : value) {
    if (layerStyle == nullptr) {
      continue;
    }
    layerStyle->attachToLayer(this);
    _layerStyles.push_back(layerStyle);
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

void Layer::setChildren(const std::vector<std::shared_ptr<Layer>>& children) {
  std::vector<std::shared_ptr<Layer>> validChildren;
  std::unordered_map<Layer*, size_t> newIndexMap;

  for (const auto& child : children) {
    if (child == nullptr || newIndexMap.find(child.get()) != newIndexMap.end()) {
      continue;
    }
    if (child.get() == this) {
      LOGE("setChildren() The child is the same as the parent.");
      continue;
    }
    if (child->doContains(this)) {
      LOGE("setChildren() The child is already a parent of the parent.");
      continue;
    }
    if (child->_root == child.get()) {
      LOGE("A root layer cannot be added as a child to another layer.");
      continue;
    }
    newIndexMap[child.get()] = validChildren.size();
    validChildren.push_back(child);
  }

  if (_children == validChildren) {
    return;
  }

  auto oldChildren = _children;
  std::vector<Layer*> nodesToMarkDirty;
  std::vector<Layer*> retainedChildren;
  std::vector<size_t> newPositions;
  std::unordered_set<Layer*> oldChildrenSet;

  for (const auto& child : oldChildren) {
    auto* ptr = child.get();
    oldChildrenSet.insert(ptr);
    auto it = newIndexMap.find(ptr);
    if (it == newIndexMap.end()) {
      nodesToMarkDirty.push_back(ptr);
      ptr->_parent = nullptr;
      ptr->onDetachFromRoot();
      if (_root) {
        _root->invalidateRect(ptr->renderBounds);
        ptr->renderBounds = {};
      }
    } else {
      retainedChildren.push_back(ptr);
      newPositions.push_back(it->second);
    }
  }

  ComputeDirtyNodesForReordering(retainedChildren, newPositions, &nodesToMarkDirty);

  std::vector<Layer*> addedChildren;
  for (const auto& child : validChildren) {
    if (oldChildrenSet.find(child.get()) == oldChildrenSet.end()) {
      child->removeFromParent();
      addedChildren.push_back(child.get());
    }
  }

  _children = std::move(validChildren);

  for (auto* child : addedChildren) {
    nodesToMarkDirty.push_back(child);
    child->_parent = this;
    child->onAttachToRoot(_root);
  }

  for (auto* dirtyNode : nodesToMarkDirty) {
    dirtyNode->invalidateTransform();
  }

  invalidateDescendents();
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
  if (computeTightBounds || bitFields.dirtyDescendents || canPreserve3D()) {
    return computeBounds(coordinateMatrix, computeTightBounds);
  }
  if (!localBounds) {
    localBounds = std::make_unique<Rect>(computeBounds(Matrix3D::I(), computeTightBounds));
  }
  return coordinateMatrix.mapRect(*localBounds);
}

static Rect ComputeContentBounds(const LayerContent& content, const Rect& contentBounds,
                                 const Matrix3D& coordinateMatrix, bool applyMatrixAtEnd,
                                 bool computeTightBounds) {
  auto contentMatrix = applyMatrixAtEnd ? Matrix3D::I() : coordinateMatrix;
  if (!computeTightBounds) {
    return contentMatrix.mapRect(contentBounds);
  }

  if (Matrix3DUtils::IsMatrix3DAffine(contentMatrix)) {
    return content.getTightBounds(Matrix3DUtils::GetMayLossyAffineMatrix(contentMatrix));
  }
  auto bounds = content.getTightBounds(Matrix::I());
  return contentMatrix.mapRect(bounds);
}

Rect Layer::computeBounds(const Matrix3D& coordinateMatrix, bool computeTightBounds) {
  auto canPreserve3D = this->canPreserve3D();
  bool hasEffects = !_layerStyles.empty() || !_filters.empty();
  // When preserving 3D, the matrix must be passed down to preserve 3D state.
  // Otherwise, when has effects, compute in local coordinates first, then apply matrix at end.
  bool isAffine = Matrix3DUtils::IsMatrix3DAffine(coordinateMatrix);
  bool applyMatrixAtEnd = !canPreserve3D && (hasEffects || !isAffine);

  Rect bounds = {};
  if (auto content = getContent()) {
    auto contentBounds = content->getBounds();
    bool behindCamera =
        !isAffine && Matrix3DUtils::IsRectBehindCamera(contentBounds, coordinateMatrix);
    if (!behindCamera) {
      bounds.join(ComputeContentBounds(*content, contentBounds, coordinateMatrix, applyMatrixAtEnd,
                                       computeTightBounds));
    }
  }

  for (const auto& child : _children) {
    // Alpha does not need to be checked; alpha == 0 is still valid.
    if (!child->visible() || child->maskOwner) {
      continue;
    }
    auto childMatrix = child->getMatrixWithScrollRect();
    if (!canPreserve3D) {
      childMatrix.setRow(2, {0, 0, 1, 0});
    }
    childMatrix.postConcat(applyMatrixAtEnd ? Matrix3D::I() : coordinateMatrix);
    auto childBounds = child->getBoundsInternal(childMatrix, computeTightBounds);
    if (child->_scrollRect) {
      auto relativeScrollRect = childMatrix.mapRect(*child->_scrollRect);
      if (!childBounds.intersect(relativeScrollRect)) {
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
    DEBUG_ASSERT(!canPreserve3D);
    auto layerBounds = bounds;
    for (auto& layerStyle : _layerStyles) {
      DEBUG_ASSERT(layerStyle != nullptr);
      auto styleBounds = layerStyle->filterBounds(layerBounds, 1.0f);
      bounds.join(styleBounds);
    }
    for (auto& filter : _filters) {
      DEBUG_ASSERT(filter != nullptr);
      bounds = filter->filterBounds(bounds, 1.0f);
    }
  }

  if (applyMatrixAtEnd) {
    if (!isAffine && Matrix3DUtils::IsRectBehindCamera(bounds, coordinateMatrix)) {
      return {};
    }
    bounds = coordinateMatrix.mapRect(bounds);
  }
  return bounds;
}

Point Layer::globalToLocal(const Point& globalPoint) const {
  auto globalMatrix = getGlobalMatrix();
  auto matrix = Matrix3DUtils::GetMayLossyMatrix(globalMatrix);
  Matrix inversedMatrix;
  if (!matrix.invert(&inversedMatrix)) {
    DEBUG_ASSERT(false);
    return Point::Make(0, 0);
  }
  Point result = globalPoint;
  inversedMatrix.mapPoints(&result, 1);
  return result;
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

  // Background styles are disabled for 3D subtrees because depth-based compositing order
  // conflicts with the requirement to draw background content first.
  if (context && canInvert && hasBackgroundStyle() && !canPreserve3D()) {
    auto scale = canvas->getMatrix().getMaxScale();
    auto backgroundRect = clippedBounds;
    backgroundRect.scale(scale, scale);
    auto backgroundMatrix = Matrix3DUtils::GetMayLossyMatrix(globalToLocalMatrix);
    backgroundMatrix.postScale(scale, scale);
    if (auto backgroundContext =
            createBackgroundContext(context, backgroundRect, backgroundMatrix,
                                    bounds == clippedBounds, surface->colorSpace())) {
      auto backgroundCanvas = backgroundContext->getCanvas();
      auto actualMatrix = backgroundCanvas->getMatrix();
      actualMatrix.preConcat(Matrix3DUtils::GetMayLossyMatrix(localToGlobalMatrix));
      backgroundCanvas->setMatrix(actualMatrix);
      Point offset = {};
      auto image = getBackgroundImage(args, scale, &offset);
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
  if (canPreserve3D()) {
    drawByStarting3DContext(args, canvas, getMatrixWithScrollRect(), &Layer::drawLayer, alpha,
                            blendMode);
  } else {
    drawLayer(args, canvas, alpha, blendMode);
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
    if (!layer->canPreserve3D()) {
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
    DEBUG_ASSERT(layerFilter != nullptr);
    if (auto filter = layerFilter->getImageFilter(contentScale)) {
      filters.push_back(filter);
    }
  }
  return ImageFilter::Compose(filters);
}

bool Layer::drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode) {
  DEBUG_ASSERT(canvas != nullptr);
  auto contentScale = canvas->getMatrix().getMaxScale();
  if (FloatNearlyZero(contentScale)) {
    return true;
  }
  if (args.renderRect && !Rect::Intersects(*args.renderRect, renderBounds)) {
    if (args.blurBackground) {
      auto backgroundRect = args.blurBackground->getBackgroundRect();
      if (!Rect::Intersects(*args.renderRect, backgroundRect)) {
        return true;
      }
      auto backgroundArgs = args;
      backgroundArgs.drawMode = DrawMode::Background;
      backgroundArgs.blurBackground = nullptr;
      backgroundArgs.renderRect = &backgroundRect;
      drawLayer(backgroundArgs, args.blurBackground->getCanvas(), alpha, blendMode);
    }
    return true;
  }
  std::shared_ptr<MaskFilter> maskFilter = nullptr;
  if (!prepareMask(args, canvas, &maskFilter)) {
    return true;
  }
  bool needsMaskFilter = maskFilter != nullptr;
  bool hasPerspective = canvas->getMatrix().hasPerspective();
  if (canUseSubtreeCache(args, blendMode, hasPerspective) &&
      drawWithSubtreeCache(args, canvas, alpha, blendMode, maskFilter)) {
    return true;
  }

  // For non-leaf layers in a 3D rendering context layer tree, direct rendering is always used.
  // Offscreen constraints (filters, styles, masks) are already checked by the parent layer at call
  // site. Group opacity, blend mode, and passThroughBackground have lower priority than enabling
  // 3D rendering context and are ignored during drawing.
  bool needsOffscreen = false;
  if (!canPreserve3D()) {
    needsOffscreen = blendMode != BlendMode::SrcOver || !bitFields.passThroughBackground ||
                     (alpha < 1.0f && bitFields.allowsGroupOpacity) ||
                     (!_filters.empty() && !args.excludeEffects) || needsMaskFilter ||
                     hasPerspective;
  }
  if (needsOffscreen) {
    // Content and children are rendered together in an offscreen buffer.
    drawOffscreen(args, canvas, alpha, blendMode, maskFilter);
  } else {
    // Content and children are rendered independently.
    drawDirectly(args, canvas, alpha);
  }
  return true;
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

std::shared_ptr<Picture> Layer::getMaskPicture(const DrawArgs& args, bool isContourMode,
                                               float scale, const Matrix3D& relativeMatrix3D) {
  // Note: Does not use layerClipBounds here. Using clipBounds may cause PathOp errors when
  // extracting maskPath from the picture, resulting in incorrect clip regions.
  DrawArgs maskArgs = args;
  maskArgs.excludeEffects |= isContourMode;
  maskArgs.blurBackground = nullptr;
  auto maskCanPreserve3D = _mask->canPreserve3D();
  // When mask enables 3D context, the full 3D relative matrix is handled by the 3D context.
  // Otherwise, use the 2D projection of the relative matrix on canvas.
  auto canvasMatrix = Matrix3DUtils::GetMayLossyMatrix(relativeMatrix3D);
  if (isContourMode) {
    auto drawMask = [&](Canvas* canvas, OpaqueContext* opaqueContext) {
      maskArgs.opaqueContext = opaqueContext;
      if (maskCanPreserve3D) {
        _mask->drawByStarting3DContext(maskArgs, canvas, relativeMatrix3D, &Layer::drawContour,
                                       _mask->_alpha, BlendMode::SrcOver);
      } else {
        canvas->concat(canvasMatrix);
        _mask->drawContour(maskArgs, canvas, _mask->_alpha, BlendMode::SrcOver);
      }
    };
    return RecordOpaquePicture(scale, drawMask);
  }
  auto drawMask = [&](Canvas* canvas) {
    if (maskCanPreserve3D) {
      _mask->drawByStarting3DContext(maskArgs, canvas, relativeMatrix3D, &Layer::drawLayer,
                                     _mask->_alpha, BlendMode::SrcOver);
    } else {
      canvas->concat(canvasMatrix);
      _mask->drawLayer(maskArgs, canvas, _mask->_alpha, BlendMode::SrcOver);
    }
  };
  return RecordPicture(scale, drawMask);
}

MaskData Layer::getMaskData(const DrawArgs& args, float scale,
                            const std::optional<Rect>& layerClipBounds) {
  DEBUG_ASSERT(_mask != nullptr);
  DEBUG_ASSERT(args.render3DContext == nullptr);
  auto maskType = static_cast<LayerMaskType>(bitFields.maskType);
  auto isContourMode = maskType == LayerMaskType::Contour;

  auto relativeMatrix3D = _mask->getRelativeMatrix(this);
  auto maskPicture = getMaskPicture(args, isContourMode, scale, relativeMatrix3D);
  if (maskPicture == nullptr) {
    return {};
  }

  bool needLuminance = maskType == LayerMaskType::Luminance;
  if (!needLuminance) {
    Path maskPath = {};
    if (MaskContext::GetMaskPath(maskPicture, &maskPath)) {
      maskPath.transform(Matrix::MakeScale(1.0f / scale, 1.0f / scale));
      return {std::move(maskPath), nullptr};
    }
  }

  Rect maskBounds = maskPicture->getBounds();
  if (layerClipBounds.has_value()) {
    auto scaledClipBounds = *layerClipBounds;
    scaledClipBounds.scale(scale, scale);
    if (!maskBounds.intersect(scaledClipBounds)) {
      return {};
    }
  }
  Point maskImageOffset = {};
  auto maskContentImage =
      ToImageWithOffset(std::move(maskPicture), &maskImageOffset, &maskBounds, args.dstColorSpace);
  if (maskContentImage == nullptr) {
    return {};
  }
  if (needLuminance) {
    maskContentImage =
        maskContentImage->makeWithFilter(ImageFilter::ColorFilter(ColorFilter::Luma()));
  }
  auto maskMatrix = Matrix::MakeScale(1.0f / scale, 1.0f / scale);
  maskMatrix.preTranslate(maskImageOffset.x, maskImageOffset.y);

  auto shader = Shader::MakeImageShader(maskContentImage, TileMode::Decal, TileMode::Decal);
  if (shader) {
    shader = shader->makeWithMatrix(maskMatrix);
  }
  return {{}, MaskFilter::MakeShader(shader)};
}

std::shared_ptr<Image> Layer::getContentContourImage(const DrawArgs& args, float contentScale,
                                                     Point* offset, bool* contourMatchesContent) {
  if (FloatNearlyZero(contentScale)) {
    return nullptr;
  }
  bool allMatch = true;
  auto picture = RecordOpaquePicture(contentScale, [&](Canvas* canvas, OpaqueContext* context) {
    auto contourArgs = args;
    contourArgs.opaqueContext = context;
    if (!drawContourInternal(contourArgs, canvas, true)) {
      allMatch = false;
    }
  });
  auto imageOffset = Point::Zero();
  auto image = ToImageWithOffset(std::move(picture), &imageOffset, nullptr, args.dstColorSpace);
  if (offset) {
    *offset = imageOffset;
  }
  if (contourMatchesContent) {
    *contourMatchesContent = allMatch;
  }
  return image;
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

bool Layer::shouldPassThroughBackground(BlendMode blendMode, bool hasPerspective) const {
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
  // (Offscreen rendering happens when: non-SrcOver blend mode OR has filters OR has perspective
  // transform)
  bool needsOffscreen = blendMode != BlendMode::SrcOver || !_filters.empty() || hasPerspective;
  if (needsOffscreen) {
    return false;
  }

  // 4. Layer does not start or extend a 3D context
  // (When starting or extending a 3D context, child layers need to maintain independent 3D states,
  // so pass-through background is not supported)
  if (canPreserve3D()) {
    return false;
  }

  return true;
}

bool Layer::canUseSubtreeCache(const DrawArgs& args, BlendMode blendMode, bool hasPerspective) {
  // If the layer can start or extend a 3D rendering context, child layers need to maintain
  // independent 3D states, so subtree caching is not supported.
  if (canPreserve3D()) {
    return false;
  }
  // The cache stores Normal mode content. Since layers with BackgroundStyle are excluded from
  // caching, the cached content can also be used for Background mode drawing.
  if (args.excludeEffects) {
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
  if (shouldPassThroughBackground(blendMode, hasPerspective) || hasBackgroundStyle()) {
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
  auto picture =
      RecordPicture(contentScale, [&](Canvas* canvas) { drawDirectly(drawArgs, canvas, 1.0f); });
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
                                 BlendMode blendMode,
                                 const std::shared_ptr<MaskFilter>& maskFilter) {
  // Non-leaf nodes in 3D rendering context layer tree have caching disabled.
  DEBUG_ASSERT(!canPreserve3D());
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
  cache->draw(args.context, longEdge, canvas, paint);
  if (args.blurBackground &&
      HasStyleSource(args.styleSourceTypes, LayerStyleExtraSourceType::Background)) {
    cache->draw(args.context, longEdge, args.blurBackground->getCanvas(), paint);
  }
  return true;
}

void Layer::drawOffscreen(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                          const std::shared_ptr<MaskFilter>& maskFilter) {
  // Non-leaf layers in a 3D rendering context layer tree never require offscreen rendering.
  DEBUG_ASSERT(args.render3DContext == nullptr);
  DEBUG_ASSERT(!canPreserve3D());

  auto imageMatrix = Matrix::I();
  std::shared_ptr<Image> image = nullptr;
  auto clipBoundsCanvas = args.blurBackground ? args.blurBackground->getCanvas() : canvas;
  auto clipBounds = GetClipBounds(clipBoundsCanvas);
  auto contentArgs = args;

  const auto hasPerspective = canvas->getMatrix().hasPerspective();
  if (shouldPassThroughBackground(blendMode, hasPerspective) && canvas->getSurface()) {
    // In pass-through mode, the image drawn to canvas contains the blended background, while
    // the image drawn to backgroundCanvas should be the layer content without background blending.
    // Use canvas's clipBounds instead of blurBackground's for pass-through rendering.
    auto canvasClipBounds = args.blurBackground ? GetClipBounds(canvas) : clipBounds;
    contentArgs.blurBackground =
        args.blurBackground ? args.blurBackground->createSubContext(renderBounds, true) : nullptr;
    image = getPassThroughContentImage(args, canvas, canvasClipBounds, &imageMatrix);
  } else {
    contentArgs.blurBackground = args.blurBackground && hasBackgroundStyle()
                                     ? args.blurBackground->createSubContext(renderBounds, true)
                                     : nullptr;
    image = getContentImage(contentArgs, canvas->getMatrix(), clipBounds, &imageMatrix);
  }

  auto invertImageMatrix = Matrix::I();
  if (image == nullptr || !imageMatrix.invert(&invertImageMatrix)) {
    return;
  }

  if (args.blurBackground && !contentArgs.blurBackground) {
    if (ShouldRasterizeForBackground(canvas, args.blurBackground)) {
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

void Layer::drawDirectly(const DrawArgs& args, Canvas* canvas, float alpha) {
  auto layerStyleSource = getLayerStyleSource(args, canvas->getMatrix());
  drawContents(args, canvas, alpha, layerStyleSource.get());
}

void Layer::drawContents(const DrawArgs& args, Canvas* canvas, float alpha,
                         const LayerStyleSource* layerStyleSource, const Layer* stopChild) {
  if (layerStyleSource) {
    drawLayerStyles(args, canvas, alpha, layerStyleSource, LayerStylePosition::Below);
  }
  auto content = getContent();
  bool hasForeground = false;
  if (content) {
    hasForeground = content->drawDefault(canvas, alpha, bitFields.allowsEdgeAntialiasing);
    if (args.blurBackground) {
      content->drawDefault(args.blurBackground->getCanvas(), alpha,
                           bitFields.allowsEdgeAntialiasing);
    }
  }
  if (!drawChildren(args, canvas, alpha, stopChild)) {
    return;
  }
  if (layerStyleSource) {
    drawLayerStyles(args, canvas, alpha, layerStyleSource, LayerStylePosition::Above);
  }
  if (hasForeground) {
    content->drawForeground(canvas, alpha, bitFields.allowsEdgeAntialiasing);
    if (args.blurBackground) {
      content->drawForeground(args.blurBackground->getCanvas(), alpha,
                              bitFields.allowsEdgeAntialiasing);
    }
  }
}

bool Layer::drawContour(const DrawArgs& args, Canvas* canvas, float, BlendMode) {
  return drawContourInternal(args, canvas, false);
}

bool Layer::drawContourInternal(const DrawArgs& args, Canvas* canvas, bool contentOnly) {
  auto* opaqueContext = args.opaqueContext;
  // Check if this layer is completely covered by opaque bounds
  auto bounds = getBounds();
  auto globalBounds = canvas->getMatrix().mapRect(bounds);
  if (opaqueContext && opaqueContext->containsOpaqueBounds(globalBounds)) {
    return true;
  }

  // Check effects for child layers (not contentOnly)
  bool allMatch = true;
  if (!contentOnly && !args.excludeEffects && (!_filters.empty() || !_layerStyles.empty())) {
    allMatch = false;
  }

  // Apply mask for child layers (not contentOnly)
  std::shared_ptr<MaskFilter> maskFilter = nullptr;
  if (!contentOnly && hasValidMask()) {
    auto contentScale = canvas->getMatrix().getMaxScale();
    auto clipBounds = GetClipBounds(canvas);
    auto maskData = getMaskData(args, contentScale, clipBounds);
    if (maskData.maskFilter == nullptr) {
      if (maskData.clipPath.isEmpty() && !maskData.clipPath.isInverseFillType()) {
        return allMatch;
      }
      canvas->clipPath(maskData.clipPath);
    } else {
      maskFilter = maskData.maskFilter;
    }
  }

  // Handle offscreen rendering for mask filter
  if (maskFilter) {
    auto contentScale = canvas->getMatrix().getMaxScale();
    auto offset = Point::Zero();
    bool contourMatchesContent = true;
    auto image = getContentContourImage(args, contentScale, &offset, &contourMatchesContent);
    if (image == nullptr) {
      return allMatch;
    }
    if (!contourMatchesContent) {
      allMatch = false;
    }
    auto imageMatrix = Matrix::MakeScale(1.0f / contentScale, 1.0f / contentScale);
    imageMatrix.preTranslate(offset.x, offset.y);
    if (image) {
      auto invertImageMatrix = Matrix::I();
      if (imageMatrix.invert(&invertImageMatrix)) {
        Paint paint = {};
        if (maskFilter) {
          paint.setMaskFilter(maskFilter->makeWithMatrix(invertImageMatrix));
        }
        AutoCanvasRestore restore(canvas);
        canvas->concat(imageMatrix);
        canvas->drawImage(std::move(image), 0, 0, &paint);
      }
    }
    return allMatch;
  }

  // Draw content's contour
  auto content = getContent();
  if (content) {
    if (!content->contourEqualsOpaqueContent()) {
      allMatch = false;
    }
    content->drawContour(canvas, bitFields.allowsEdgeAntialiasing);
  }

  // Draw children's contour directly (not using drawChildren to avoid background logic)
  auto childArgs = args;
  for (auto& child : _children) {
    if (child->maskOwner || !child->visible() || child->_alpha <= 0) {
      continue;
    }
    auto childArgsOpt = createChildArgs(childArgs, canvas, child.get(), true);
    if (!childArgsOpt.has_value()) {
      continue;
    }
    if (!drawChild(*childArgsOpt, canvas, child.get(), 1.0f, &Layer::drawContour)) {
      allMatch = false;
    }
  }
  return allMatch;
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

  bool skipBackground =
      !HasStyleSource(args.styleSourceTypes, LayerStyleExtraSourceType::Background);

  for (size_t i = 0; i < _children.size(); ++i) {
    auto& child = _children[i];
    if (child.get() == stopChild) {
      return false;
    }
    if (child->maskOwner || !child->visible() || child->_alpha <= 0) {
      continue;
    }
    auto childArgsOpt = createChildArgs(args, canvas, child.get(), skipBackground);
    if (!childArgsOpt.has_value()) {
      continue;
    }
    auto& childArgs = *childArgsOpt;
    auto childIndex = static_cast<int>(i);
    if (childIndex < lastBackgroundLayerIndex) {
      childArgs.forceDrawBackground = true;
    } else {
      childArgs.forceDrawBackground = false;
      if (childIndex > lastBackgroundLayerIndex) {
        childArgs.blurBackground = nullptr;
      }
    }
    drawChild(childArgs, canvas, child.get(), alpha, &Layer::drawLayer);
  }
  return true;
}

void Layer::drawByStarting3DContext(const DrawArgs& args, Canvas* canvas, const Matrix3D& matrix3D,
                                    LayerDrawFunc drawFunc, float alpha, BlendMode blendMode) {
  DEBUG_ASSERT(canPreserve3D());
  DEBUG_ASSERT(args.render3DContext == nullptr);

  const auto bounds = getBoundsInternal(matrix3D, false);
  const auto newContext = Create3DContext(args, canvas, bounds);
  if (newContext == nullptr) {
    return;
  }

  auto contextArgs = args;
  contextArgs.render3DContext = newContext;
  // Layers inside a 3D rendering context need to maintain independent 3D state. This means layers
  // drawn later may become the background, making it impossible to know the final background when
  // drawing each layer. Therefore, background styles are disabled.
  contextArgs.styleSourceTypes = StyleSourceTypesFor3DContext;

  const auto offscreenCanvas =
      newContext->beginRecording(matrix3D, bitFields.allowsEdgeAntialiasing);
  contextArgs.opaqueContext = newContext->currentOpaqueContext();
  (this->*drawFunc)(contextArgs, offscreenCanvas, alpha, blendMode);
  newContext->endRecording();
  newContext->finishAndDrawTo(canvas, bitFields.allowsEdgeAntialiasing);
}

std::optional<DrawArgs> Layer::createChildArgs(const DrawArgs& args, Canvas* canvas, Layer* child,
                                               bool skipBackground) {
  auto childArgs = args;
  if (skipBackground) {
    RemoveStyleSource(childArgs.styleSourceTypes, LayerStyleExtraSourceType::Background);
    childArgs.blurBackground = nullptr;
  }
  // Handle 3D context state transitions:
  // - If parent has no 3D context and child can preserve 3D, child becomes the root of a new 3D
  //   subtree, so create a new context.
  // - If parent has 3D context but child cannot preserve 3D, child is a leaf node in the 3D
  //   subtree. Clear render3DContext to prevent child layers from participating in 3D compositing.
  //   The entire subtree is rendered as a flat image with this layer's 3D transform applied.
  auto childCanPreserve3D = child->canPreserve3D();
  if (!args.render3DContext && childCanPreserve3D) {
    auto childBounds = child->getBounds(this);
    if (childBounds.isEmpty()) {
      return std::nullopt;
    }
    childArgs.render3DContext = Create3DContext(childArgs, canvas, childBounds);
    if (childArgs.render3DContext == nullptr) {
      return std::nullopt;
    }
    // Layers inside a 3D rendering context need to maintain independent 3D state. This means
    // layers drawn later may become the background, making it impossible to know the final
    // background when drawing each layer. Therefore, background styles are disabled.
    childArgs.styleSourceTypes = StyleSourceTypesFor3DContext;
    childArgs.blurBackground = nullptr;
  }
  return childArgs;
}

bool Layer::drawChild(const DrawArgs& childArgs, Canvas* canvas, Layer* child, float alpha,
                      LayerDrawFunc drawFunc) {
  AutoCanvasRestore autoRestore(canvas);
  auto backgroundCanvas =
      childArgs.blurBackground ? childArgs.blurBackground->getCanvas() : nullptr;
  AutoCanvasRestore autoRestoreBg(backgroundCanvas);

  auto childTransform3D = child->getMatrixWithScrollRect();
  auto context3D = childArgs.render3DContext.get();
  // For layers outside a 3D context, matrices are applied to the canvas directly.
  // For layers inside a 3D context, matrices are handled by render3DContext.
  const auto canvasMatrix =
      context3D == nullptr ? Matrix3DUtils::GetMayLossyMatrix(childTransform3D) : Matrix::I();
  const auto* scrollRectTransform = context3D == nullptr ? nullptr : &childTransform3D;
  canvas->concat(canvasMatrix);
  ClipScrollRect(canvas, child->_scrollRect.get(), scrollRectTransform);
  if (backgroundCanvas) {
    backgroundCanvas->concat(canvasMatrix);
    ClipScrollRect(backgroundCanvas, child->_scrollRect.get(), scrollRectTransform);
  }

  Canvas* targetCanvas = canvas;
  auto drawArgs = childArgs;
  if (context3D) {
    targetCanvas =
        context3D->beginRecording(childTransform3D, child->bitFields.allowsEdgeAntialiasing);
    drawArgs.opaqueContext = context3D->currentOpaqueContext();
  }
  // If child cannot preserve 3D but has a 3D context, clear it so that child's internal rendering
  // (e.g., drawOffscreen) doesn't see it. The child is still recorded into the 3D context via
  // beginRecording/endRecording above.
  if (context3D && !child->canPreserve3D()) {
    drawArgs.render3DContext = nullptr;
  }

  auto blendMode = static_cast<BlendMode>(child->bitFields.blendMode);
  bool result = (child->*drawFunc)(drawArgs, targetCanvas, child->_alpha * alpha, blendMode);

  if (context3D) {
    context3D->endRecording();
    if (context3D->isFinished()) {
      context3D->finishAndDrawTo(canvas, child->bitFields.allowsEdgeAntialiasing);
    }
  }
  return result;
}

float Layer::drawBackgroundLayers(const DrawArgs& args, Canvas* canvas) {
  if (!_parent) {
    return _alpha;
  }
  // parent background -> parent layer styles (below) -> parent content -> sibling layers content
  auto currentAlpha = _parent->drawBackgroundLayers(args, canvas);
  auto layerStyleSource = _parent->getLayerStyleSource(args, canvas->getMatrix());
  _parent->drawContents(args, canvas, currentAlpha, layerStyleSource.get(), this);
  canvas->concat(Matrix3DUtils::GetMayLossyMatrix(getMatrixWithScrollRect()));
  if (_scrollRect) {
    canvas->clipRect(*_scrollRect);
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

  auto source = std::make_unique<LayerStyleSource>();
  source->contentScale = contentScale;

  DrawArgs drawArgs = args;
  drawArgs.blurBackground = nullptr;
  drawArgs.excludeEffects = bitFields.excludeChildEffectsInLayerStyle;
  // Layer style source content should be rendered to a regular texture, not to 3D compositor.
  drawArgs.render3DContext = nullptr;

  auto needContour =
      excludeContour
          ? false
          : std::any_of(_layerStyles.begin(), _layerStyles.end(), [](const auto& layerStyle) {
              return layerStyle->extraSourceType() == LayerStyleExtraSourceType::Contour;
            });

  // Draw contour and check if all drawn content's contour matches its opaque content.
  // When true, we can reuse the contour image as content image.
  bool allContourMatch = false;
  if (needContour) {
    source->contour =
        getContentContourImage(drawArgs, contentScale, &source->contourOffset, &allContourMatch);
    if (source->contour == nullptr) {
      return nullptr;
    }
    if (allContourMatch) {
      // Contour is identical to content for all drawn children, reuse contour image as content.
      source->content = source->contour;
      source->contentOffset = source->contourOffset;
    }
  }

  // Need to draw content separately when any drawn child's contour differs from content.
  if (!allContourMatch) {
    auto picture = RecordOpaquePicture(contentScale, [&](Canvas* canvas, OpaqueContext*) {
      drawContents(drawArgs, canvas, 1.0f);
    });
    source->content =
        ToImageWithOffset(std::move(picture), &source->contentOffset, nullptr, args.dstColorSpace);
    if (source->content == nullptr) {
      return nullptr;
    }
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
  auto localToGlobalMatrix = Matrix3DUtils::GetMayLossyMatrix(getGlobalMatrix());
  Matrix globalToLocalMatrix = {};
  if (!localToGlobalMatrix.invert(&globalToLocalMatrix)) {
    return nullptr;
  }
  canvas->concat(globalToLocalMatrix);
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
  // Apply the content transform matrix to canvas instead of pictureCanvas to avoid blurry results
  // when scaled up, since picture recording at a smaller scale loses resolution.
  AutoCanvasRestore restoreCanvas(canvas);
  AutoCanvasRestore restoreBackground(backgroundCanvas);
  auto matrix = Matrix::MakeScale(1.f / source->contentScale, 1.f / source->contentScale);
  matrix.preTranslate(source->contentOffset.x, source->contentOffset.y);
  canvas->concat(matrix);
  if (backgroundCanvas) {
    backgroundCanvas->concat(matrix);
  }
  auto clipBounds =
      args.blurBackground ? GetClipBounds(args.blurBackground->getCanvas()) : std::nullopt;
  for (const auto& layerStyle : _layerStyles) {
    DEBUG_ASSERT(layerStyle != nullptr);
    if (layerStyle->position() != position ||
        !HasStyleSource(args.styleSourceTypes, layerStyle->extraSourceType())) {
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
    } else if (!clipBounds.has_value() ||
               !ShouldRasterizeForBackground(canvas, args.blurBackground)) {
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
    // Filters and styles interrupt 3D rendering context, so non-root layers inside 3D rendering
    // context can ignore parent filters and styles when calculating dirty regions.
    DEBUG_ASSERT(!canPreserve3D());
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
    std::shared_ptr<RegionTransformer> childTransformer = nullptr;
    if (canPreserve3D() || child->canPreserve3D()) {
      // Child is inside a 3D rendering context - allow combining matrices.
      childTransformer = RegionTransformer::MakeFromMatrix3D(childMatrix, transformer, true);
    } else {
      // Child is outside 3D context - project to 2D plane.
      childTransformer = RegionTransformer::MakeFromMatrix(
          Matrix3DUtils::GetMayLossyMatrix(childMatrix), transformer);
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
    child->updateRenderBounds(std::move(childTransformer), childForceDirty);
    child->bitFields.dirtyTransform = false;
    if (!child->maskOwner) {
      renderBounds.join(child->renderBounds);
    }
  }
  auto backOutset = 0.f;
  if (!renderBounds.isEmpty()) {
    for (auto& style : _layerStyles) {
      DEBUG_ASSERT(style != nullptr);
      if (style->extraSourceType() != LayerStyleExtraSourceType::Background) {
        continue;
      }
      auto outset = style->filterBackground(Rect::MakeEmpty(), contentScale);
      backOutset = std::max(backOutset, outset.right);
      backOutset = std::max(backOutset, outset.bottom);
    }
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
    auto childTransformer = RegionTransformer::MakeFromMatrix(
        Matrix3DUtils::GetMayLossyMatrix(childMatrix), transformer);
    child->checkBackgroundStyles(childTransformer);
  }
  updateBackgroundBounds(transformer ? transformer->getMaxScale() : 1.0f);
}

void Layer::updateBackgroundBounds(float contentScale) {
  for (auto& style : _layerStyles) {
    DEBUG_ASSERT(style != nullptr);
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
    DEBUG_ASSERT(style != nullptr);
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

bool Layer::canPreserve3D() const {
  return _preserve3D && _filters.empty() && _layerStyles.empty() && !hasValidMask() &&
         _scrollRect == nullptr;
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
