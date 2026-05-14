/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "Context3DCompositor.h"
#include <algorithm>
#include "BspTree.h"
#include "core/Matrix3DUtils.h"
#include "core/images/TextureImage.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/QuadRecord.h"
#include "gpu/QuadsVertexProvider.h"
#include "gpu/ops/Quads3DDrawOp.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

// Tolerance for determining if a vertex lies on the original rectangle's edge.
static constexpr float AA_EPSILON = 0.01f;

// Flags indicating the edges of a rectangle.
static constexpr unsigned RECT_EDGE_LEFT = 0b1000;
static constexpr unsigned RECT_EDGE_TOP = 0b0001;
static constexpr unsigned RECT_EDGE_RIGHT = 0b0010;
static constexpr unsigned RECT_EDGE_BOTTOM = 0b0100;

// The color space used for the compositor's render target.
static const auto TargetColorSpace = ColorSpace::SRGB;

static inline AAType GetAAType(int sampleCount, bool antiAlias) {
  if (sampleCount > 1) {
    return AAType::MSAA;
  }
  if (antiAlias) {
    return AAType::Coverage;
  }
  return AAType::None;
}

/**
 * Determines which edges of the rect this point lies on.
 */
static unsigned DeterminePointOnRectEdge(const Point& point, const Rect& rect) {
  unsigned edges = 0;
  if (std::abs(point.x - rect.left) < AA_EPSILON) {
    edges |= RECT_EDGE_LEFT;
  }
  if (std::abs(point.x - rect.right) < AA_EPSILON) {
    edges |= RECT_EDGE_RIGHT;
  }
  if (std::abs(point.y - rect.top) < AA_EPSILON) {
    edges |= RECT_EDGE_TOP;
  }
  if (std::abs(point.y - rect.bottom) < AA_EPSILON) {
    edges |= RECT_EDGE_BOTTOM;
  }
  return edges;
}

/**
 * Returns true if both endpoints of a quad edge lie on the same edge of the original rectangle,
 * indicating that this edge is an exterior edge of the original rectangle.
 */
static bool IsExteriorEdge(unsigned cornerAEdge, unsigned cornerBEdge) {
  return (cornerAEdge & cornerBEdge) != 0;
}

/**
 * Computes per-edge AA flags for a quad. An edge needs AA if both endpoints lie on the same
 * edge of the original rect (i.e., it's an exterior edge, not a BSP split edge).
 */
static unsigned GetQuadAAFlags(const Quad& quad, const Rect& rect) {
  const unsigned p0 = DeterminePointOnRectEdge(quad.point(0), rect);
  const unsigned p1 = DeterminePointOnRectEdge(quad.point(1), rect);
  const unsigned p2 = DeterminePointOnRectEdge(quad.point(2), rect);
  const unsigned p3 = DeterminePointOnRectEdge(quad.point(3), rect);

  unsigned aaFlags = QUAD_AA_FLAG_NONE;
  if (IsExteriorEdge(p0, p1)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_0;
  }
  if (IsExteriorEdge(p1, p3)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_1;
  }
  if (IsExteriorEdge(p2, p0)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_2;
  }
  if (IsExteriorEdge(p3, p2)) {
    aaFlags |= QUAD_AA_FLAG_EDGE_3;
  }
  return aaFlags;
}

/**
 * Returns the quad to draw and its AA flags based on whether it's a sub-quad or the original rect.
 */
static std::pair<Quad, unsigned> GetQuadAndAAFlags(const Rect& originalRect, AAType aaType,
                                                   const Quad* subQuad) {
  if (subQuad != nullptr) {
    auto aaFlags = QUAD_AA_FLAG_NONE;
    if (aaType == AAType::Coverage) {
      aaFlags = GetQuadAAFlags(*subQuad, originalRect);
    }
    return {*subQuad, aaFlags};
  }
  auto quad = Quad::MakeFrom(originalRect);
  auto aaFlags = (aaType == AAType::Coverage) ? QUAD_AA_FLAG_ALL : QUAD_AA_FLAG_NONE;
  return {quad, aaFlags};
}

Context3DCompositor::Context3DCompositor(const Context& context, int width, int height)
    : _width(width), _height(height) {
  _targetColorProxy =
      context.proxyProvider()->createRenderTargetProxy({}, width, height, PixelFormat::RGBA_8888);
  DEBUG_ASSERT(_targetColorProxy != nullptr);
}

void Context3DCompositor::addPolygon(Layer* layer, const Rect& localBounds, const Matrix3D& matrix,
                                     int depth, float alpha, bool antiAlias) {
  const auto sequenceIndex = _depthSequenceCounters[depth]++;
  auto polygon = std::make_unique<DrawPolygon3D>(layer, localBounds, matrix, depth, sequenceIndex,
                                                 alpha, antiAlias);
  _polygons.push_back(std::move(polygon));
}

void Context3DCompositor::drawPolygon(const DrawPolygon3D* polygon,
                                      const std::shared_ptr<Image>& image) {
  if (image == nullptr) {
    return;
  }
  if (!polygon->isSplit()) {
    drawQuads(polygon, image, {});
  } else {
    drawQuads(polygon, image, polygon->toQuads());
  }
}

namespace {
// Functor adapter so BspTree::traverseBackToFront — which expects a callable taking
// const DrawPolygon3D* — can collect fragments into a vector without a lambda.
struct CollectFragmentsAction {
  std::vector<DrawPolygon3D*>* output = nullptr;

  void operator()(const DrawPolygon3D* polygon) const {
    output->push_back(const_cast<DrawPolygon3D*>(polygon));
  }
};
}  // namespace

void Context3DCompositor::drawQuads(const DrawPolygon3D* polygon,
                                    const std::shared_ptr<Image>& image,
                                    const std::vector<Quad>& subQuads) {
  DEBUG_ASSERT(_targetColorProxy != nullptr);
  auto context = _targetColorProxy->getContext();
  DEBUG_ASSERT(context != nullptr);
  auto aaType = GetAAType(_targetColorProxy->sampleCount(), polygon->antiAlias());
  const auto& localBounds = polygon->localBounds();
  Rect originalRect = localBounds;

  auto allocator = context->drawingAllocator();
  // Decide whether the polygon's alpha is applied here (group opacity) or has already been baked
  // into `image` by Layer::drawLayer's recursive raster pass:
  //   - allowsGroupOpacity=true  -> raster uses alpha=1, multiply by polygon.alpha here.
  //   - allowsGroupOpacity=false -> raster bakes polygon.alpha into `image`, leave vertex at 1.
  auto* polygonLayer = polygon->layer();
  float vertexAlpha =
      (polygonLayer != nullptr && polygonLayer->allowsGroupOpacity()) ? polygon->alpha() : 1.0f;
  Color vertexColor(1, 1, 1, vertexAlpha);
  auto matrix = polygon->matrix().asMatrix();
  std::vector<PlacementPtr<QuadRecord>> quadRecords;
  if (subQuads.empty()) {
    auto [quad, aaFlags] = GetQuadAndAAFlags(originalRect, aaType, nullptr);
    quadRecords.push_back(allocator->make<QuadRecord>(quad, aaFlags, vertexColor, matrix));
  } else {
    for (const auto& subQuad : subQuads) {
      auto [quad, aaFlags] = GetQuadAndAAFlags(originalRect, aaType, &subQuad);
      quadRecords.push_back(allocator->make<QuadRecord>(quad, aaFlags, vertexColor, matrix));
    }
  }

  auto vertexProvider =
      QuadsVertexProvider::MakeFrom(allocator, std::move(quadRecords), aaType, TargetColorSpace());
  auto drawOp = Quads3DDrawOp::Make(context, std::move(vertexProvider), 0);

  const SamplingArgs samplingArgs = {TileMode::Clamp, TileMode::Clamp, {}, SrcRectConstraint::Fast};
  auto textureImage = image->makeTextureImage(context);
  if (textureImage == nullptr) {
    return;
  }
  auto sourceTextureProxy = std::static_pointer_cast<TextureImage>(textureImage)->getTextureProxy();
  // Map quad-local coordinates (within polygon->localBounds) to texture UV space. The texture
  // proxy size may differ from the image size due to draw scaling, so the UV factor scales by the
  // actual texture dimensions.
  DEBUG_ASSERT(localBounds.width() > 0 && localBounds.height() > 0);
  auto uvMatrix = Matrix::MakeTrans(-localBounds.left, -localBounds.top);
  uvMatrix.postScale(static_cast<float>(sourceTextureProxy->width()) / localBounds.width(),
                     static_cast<float>(sourceTextureProxy->height()) / localBounds.height());
  auto fragmentProcessor =
      TextureEffect::Make(allocator, std::move(sourceTextureProxy), samplingArgs, &uvMatrix);
  drawOp->addColorFP(std::move(fragmentProcessor));
  _drawOps.emplace_back(std::move(drawOp));
}

const std::vector<DrawPolygon3D*>& Context3DCompositor::prepareTraversal() {
  _orderedFragments.clear();
  if (_polygons.empty()) {
    return _orderedFragments;
  }
  // Sort polygons by (depth, sequenceIndex) to ensure correct paint order in BSP tree.
  // TODO: Support pre-order traversal of layers to avoid the performance cost of sorting.
  std::sort(_polygons.begin(), _polygons.end(), DrawPolygon3DOrder);
  _bspTree = std::make_unique<BspTree>(std::move(_polygons));
  _polygons.clear();
  CollectFragmentsAction action{&_orderedFragments};
  _bspTree->traverseBackToFront(action);
  return _orderedFragments;
}

std::shared_ptr<Image> Context3DCompositor::snapshotTarget() {
  DEBUG_ASSERT(_targetColorProxy != nullptr);
  auto context = _targetColorProxy->getContext();
  DEBUG_ASSERT(context != nullptr);
  // Submit accumulated ops; first submission also clears the target.
  if (!_drawOps.empty() || !_targetCleared) {
    auto opArray = context->drawingAllocator()->makeArray(std::move(_drawOps));
    auto clearColor =
        _targetCleared ? std::optional<PMColor>{} : std::optional<PMColor>(PMColor::Transparent());
    context->drawingManager()->addOpsRenderTask(_targetColorProxy, std::move(opArray), clearColor);
    _targetCleared = true;
  }
  // Make an immutable copy so subsequent draws to _targetColorProxy do not mutate this snapshot.
  auto copyProxy = _targetColorProxy->makeTextureProxy();
  context->drawingManager()->addRenderTargetCopyTask(_targetColorProxy, copyProxy);
  return TextureImage::Wrap(std::move(copyProxy), TargetColorSpace());
}

void Context3DCompositor::primeWithImage(const std::shared_ptr<Image>& image) {
  if (image == nullptr || _targetColorProxy == nullptr) {
    return;
  }
  auto width = static_cast<float>(_targetColorProxy->width());
  auto height = static_cast<float>(_targetColorProxy->height());
  if (width <= 0 || height <= 0) {
    return;
  }
  // Construct a full-target polygon with identity transform so the image lands one-to-one in
  // compositor pixel space. Layer pointer is unused here (drawQuads only reads localBounds /
  // matrix / alpha / antiAlias), but DrawPolygon3D's constructor asserts on null — pass the
  // raw image as a placeholder image, since drawQuads ignores polygon->image() in favour of
  // the explicit `image` parameter.
  auto fullRect = Rect::MakeWH(width, height);
  auto identity3D = Matrix3D::I();
  auto polygon = std::make_unique<DrawPolygon3D>(/*layer=*/nullptr, fullRect, identity3D,
                                                 /*depth=*/0, /*sequenceIndex=*/0,
                                                 /*alpha=*/1.0f, /*antiAlias=*/false);
  drawQuads(polygon.get(), image, {});
}

std::shared_ptr<Image> Context3DCompositor::flushToImage() {
  auto context = _targetColorProxy->getContext();
  DEBUG_ASSERT(context != nullptr);
  auto opArray = context->drawingAllocator()->makeArray(std::move(_drawOps));
  auto clearColor =
      _targetCleared ? std::optional<PMColor>{} : std::optional<PMColor>(PMColor::Transparent());
  context->drawingManager()->addOpsRenderTask(_targetColorProxy, std::move(opArray), clearColor);
  auto image = TextureImage::Wrap(_targetColorProxy->asTextureProxy(), TargetColorSpace());
  _targetColorProxy = nullptr;
  _orderedFragments.clear();
  _bspTree.reset();
  _targetCleared = false;
  return image;
}

}  // namespace tgfx
