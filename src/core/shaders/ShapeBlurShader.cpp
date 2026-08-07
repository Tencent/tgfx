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

#include "core/shaders/ShapeBlurShader.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "gpu/processors/RRectBlurFragmentProcessor.h"
#include "gpu/processors/RRectInnerShadowFragmentProcessor.h"
#include "gpu/processors/RectBlurFragmentProcessor.h"
#include "gpu/processors/RectInnerShadowFragmentProcessor.h"

namespace tgfx {

// Returns whether the shape can be evaluated by the closed form: it must be non-empty and carry at
// most one distinct corner radius, since per-corner radii would need a segmented row-span formula.
static inline bool IsAnalytic(const RRect& rRect) {
  return !rRect.rect().isEmpty() && !rRect.isComplex();
}

std::shared_ptr<Shader> ShapeBlurShader::Make(const RRect& rRect, float sigmaX, float sigmaY,
                                              const Color& color, float localScale) {
  if (sigmaX <= 0.0f || sigmaY <= 0.0f || !FloatsAreFinite(&sigmaX, 1) ||
      !FloatsAreFinite(&sigmaY, 1)) {
    return nullptr;
  }
  if (localScale <= 0.0f) {
    return nullptr;
  }
  if (!IsAnalytic(rRect)) {
    return nullptr;
  }
  return std::make_shared<ShapeBlurShader>(rRect, std::nullopt, Point::Zero(), sigmaX, sigmaY,
                                           color, localScale);
}

std::shared_ptr<Shader> ShapeBlurShader::MakeInner(const RRect& maskRRect, const RRect& shadowRRect,
                                                   const Point& shadowCenterOffset, float sigmaX,
                                                   float sigmaY, const Color& color,
                                                   float localScale) {
  if (sigmaX <= 0.0f || sigmaY <= 0.0f || !FloatsAreFinite(&sigmaX, 1) ||
      !FloatsAreFinite(&sigmaY, 1)) {
    return nullptr;
  }
  if (localScale <= 0.0f) {
    return nullptr;
  }
  if (!IsAnalytic(maskRRect) || !IsAnalytic(shadowRRect)) {
    return nullptr;
  }
  // The mask distance field takes a single radius, so an elliptical corner cannot be expressed. Only
  // the shadow is sigma-normalized, so only it may end up elliptical.
  const auto maskRadius = maskRRect.radii()[0];
  if (!FloatNearlyEqual(maskRadius.x, maskRadius.y)) {
    return nullptr;
  }
  return std::make_shared<ShapeBlurShader>(shadowRRect, maskRRect, shadowCenterOffset, sigmaX,
                                           sigmaY, color, localScale);
}

ShapeBlurShader::ShapeBlurShader(const RRect& rRect, const std::optional<RRect>& maskRRect,
                                 const Point& shadowCenterOffset, float sigmaX, float sigmaY,
                                 const Color& color, float localScale)
    : rRect(rRect), maskRRect(maskRRect), shadowCenterOffset(shadowCenterOffset), sigmaX(sigmaX),
      sigmaY(sigmaY), color(color), localScale(localScale) {
}

bool ShapeBlurShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::ShapeBlur) {
    return false;
  }
  // Every shadow allocates its own shader, so pointer comparison in CompareBrush always fails and
  // batching depends entirely on this field-by-field check. Comparing less than all of the state
  // would let shadows with different geometry merge into one op and render with the first one's
  // parameters.
  auto other = static_cast<const ShapeBlurShader*>(shader);
  return rRect == other->rRect && maskRRect == other->maskRRect &&
         shadowCenterOffset == other->shadowCenterOffset && sigmaX == other->sigmaX &&
         sigmaY == other->sigmaY && color == other->color && localScale == other->localScale;
}

PlacementPtr<FragmentProcessor> ShapeBlurShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  if (maskRRect.has_value()) {
    return makeInnerProcessor(args, uvMatrix, dstColorSpace);
  }
  const auto bounds = rRect.rect();
  const auto center = Point::Make(bounds.centerX(), bounds.centerY());
  // Maps canvas local coordinates to the sigma-normalized space centered on the shape: first into
  // the space the geometry lives in, then to the center, then divided by sigma per axis. The
  // incoming uvMatrix (if any) already maps the draw's coordinates into our local space, so it
  // applies before all of that.
  auto totalMatrix = Matrix::MakeScale(localScale, localScale);
  totalMatrix.postTranslate(-center.x, -center.y);
  totalMatrix.postScale(1.0f / sigmaX, 1.0f / sigmaY);
  if (uvMatrix != nullptr) {
    totalMatrix.preConcat(*uvMatrix);
  }

  const auto halfSizeOverSigma =
      Point::Make(bounds.width() * 0.5f / sigmaX, bounds.height() * 0.5f / sigmaY);
  const auto pmColor = ToPMColor(color, dstColorSpace);
  auto allocator = args.context->drawingAllocator();
  if (rRect.isRect()) {
    return RectBlurFragmentProcessor::Make(allocator, halfSizeOverSigma, pmColor, &totalMatrix);
  }
  // Simple and Oval types share a single radius across all four corners. Sigma normalization
  // divides each axis by its own sigma, so a circular radius becomes two semi-axes.
  const auto radius = rRect.radii()[0];
  const auto cornerOverSigma = Point::Make(radius.x / sigmaX, radius.y / sigmaY);
  return RRectBlurFragmentProcessor::Make(allocator, halfSizeOverSigma, cornerOverSigma, pmColor,
                                          &totalMatrix);
}

PlacementPtr<FragmentProcessor> ShapeBlurShader::makeInnerProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  const auto maskBounds = maskRRect->rect();
  const auto maskCenter = Point::Make(maskBounds.centerX(), maskBounds.centerY());
  // Unlike the outer shadow, the coordinates handed to the processor stay unnormalized: the mask's
  // antialiased edge is one pixel wide regardless of sigma, so it has to be measured in the space
  // the geometry lives in. The processor divides by sigma itself for the shadow term.
  auto totalMatrix = Matrix::MakeScale(localScale, localScale);
  totalMatrix.postTranslate(-maskCenter.x, -maskCenter.y);
  if (uvMatrix != nullptr) {
    totalMatrix.preConcat(*uvMatrix);
  }

  const auto shadowBounds = rRect.rect();
  const auto shadowHalfOverSigma =
      Point::Make(shadowBounds.width() * 0.5f / sigmaX, shadowBounds.height() * 0.5f / sigmaY);
  const auto invSigma = Point::Make(1.0f / sigmaX, 1.0f / sigmaY);
  const auto maskHalfSize = Point::Make(maskBounds.width() * 0.5f, maskBounds.height() * 0.5f);
  const auto pmColor = ToPMColor(color, dstColorSpace);
  auto allocator = args.context->drawingAllocator();
  // The mask radius is circular, so either component describes it. MakeInner rejects the rest.
  const auto maskCornerRadius = maskRRect->radii()[0].x;
  if (rRect.isRect() && maskRRect->isRect()) {
    return RectInnerShadowFragmentProcessor::Make(allocator, shadowHalfOverSigma,
                                                  shadowCenterOffset, invSigma, maskHalfSize,
                                                  pmColor, &totalMatrix);
  }
  const auto shadowRadius = rRect.radii()[0];
  const auto shadowCornerOverSigma = Point::Make(shadowRadius.x / sigmaX, shadowRadius.y / sigmaY);
  return RRectInnerShadowFragmentProcessor::Make(
      allocator, shadowHalfOverSigma, shadowCornerOverSigma, shadowCenterOffset, invSigma,
      maskHalfSize, maskCornerRadius, pmColor, &totalMatrix);
}
}  // namespace tgfx
