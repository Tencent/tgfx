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

#include "core/shaders/RRectInnerShadowShader.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "gpu/processors/RRectBlurFragmentProcessor.h"
#include "gpu/processors/RRectInnerShadowFragmentProcessor.h"

namespace tgfx {

// Returns true when a corner semi-axis cannot fit inside the rect. FloatNearlyEqual absorbs the one
// ULP by which RRect's radius scaling may overshoot the half size.
static inline bool ExceedsHalfSize(const Point& radius, const Rect& rect) {
  const auto halfWidth = rect.width() * 0.5f;
  const auto halfHeight = rect.height() * 0.5f;
  return (radius.x > halfWidth && !FloatNearlyEqual(radius.x, halfWidth)) ||
         (radius.y > halfHeight && !FloatNearlyEqual(radius.y, halfHeight));
}

std::shared_ptr<Shader> RRectInnerShadowShader::Make(const Rect& maskRect, const Point& maskRadius,
                                                     const Rect& shadowRect,
                                                     const Point& shadowRadius, float sigmaX,
                                                     float sigmaY, const Color& color) {
  if (sigmaX <= 0.0f || sigmaY <= 0.0f || !FloatsAreFinite(&sigmaX, 1) ||
      !FloatsAreFinite(&sigmaY, 1)) {
    return nullptr;
  }
  if (maskRect.isEmpty() || shadowRect.isEmpty()) {
    return nullptr;
  }
  // RRect scales radii to fit its rect, so a larger radius means the caller bypassed that. The
  // shader would clamp it and draw a shape the caller did not ask for, so fall back instead.
  const auto invalidRadius =
      ExceedsHalfSize(maskRadius, maskRect) || ExceedsHalfSize(shadowRadius, shadowRect);
  DEBUG_ASSERT(!invalidRadius);
  if (invalidRadius) {
    return nullptr;
  }
  auto shader = std::make_shared<RRectInnerShadowShader>(maskRect, maskRadius, shadowRect,
                                                         shadowRadius, sigmaX, sigmaY, color);
  shader->weakThis = shader;
  return shader;
}

RRectInnerShadowShader::RRectInnerShadowShader(const Rect& maskRect, const Point& maskRadius,
                                               const Rect& shadowRect, const Point& shadowRadius,
                                               float sigmaX, float sigmaY, const Color& color)
    : maskRect(maskRect), maskRadius(maskRadius), shadowRect(shadowRect),
      shadowRadius(shadowRadius), sigmaX(sigmaX), sigmaY(sigmaY), color(color) {
}

bool RRectInnerShadowShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::RRectInnerShadow) {
    return false;
  }
  // Every shadow allocates its own shader, so pointer comparison in CompareBrush always fails and
  // batching depends entirely on this field-by-field check. Comparing less than all of the state
  // would let shadows with different geometry merge into one op and render with the first one's
  // parameters.
  auto other = static_cast<const RRectInnerShadowShader*>(shader);
  return maskRect == other->maskRect && maskRadius == other->maskRadius &&
         shadowRect == other->shadowRect && shadowRadius == other->shadowRadius &&
         sigmaX == other->sigmaX && sigmaY == other->sigmaY && color == other->color;
}

PlacementPtr<FragmentProcessor> RRectInnerShadowShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  const auto maskCenter = Point::Make(maskRect.centerX(), maskRect.centerY());
  // Unlike the outer shadow, the mask coordinates stay unnormalized: the mask's antialiased edge is
  // one pixel wide regardless of sigma, so it has to be measured in the space the geometry lives
  // in. The shadow term gets its own sigma-normalized space instead.
  auto maskCoordMatrix = Matrix::MakeTrans(-maskCenter.x, -maskCenter.y);
  if (uvMatrix != nullptr) {
    maskCoordMatrix.preConcat(*uvMatrix);
  }
  auto shadowCoordMatrix = maskCoordMatrix;
  shadowCoordMatrix.postTranslate(maskCenter.x - shadowRect.centerX(),
                                  maskCenter.y - shadowRect.centerY());
  shadowCoordMatrix.postScale(1.0f / sigmaX, 1.0f / sigmaY);

  const auto shadowHalfOverSigma =
      Point::Make(shadowRect.width() * 0.5f / sigmaX, shadowRect.height() * 0.5f / sigmaY);
  const auto maskHalfSize = Point::Make(maskRect.width() * 0.5f, maskRect.height() * 0.5f);
  const auto shadowCornerOverSigma = Point::Make(shadowRadius.x / sigmaX, shadowRadius.y / sigmaY);
  const auto pmColor = ToPMColor(color, dstColorSpace);
  auto allocator = args.context->drawingAllocator();
  return RRectInnerShadowFragmentProcessor::Make(
      allocator, shadowHalfOverSigma, shadowCornerOverSigma, maskHalfSize, maskRadius, pmColor,
      maskCoordMatrix, shadowCoordMatrix, SelectRRectBlurQuadratureCount(shadowCornerOverSigma));
}
}  // namespace tgfx
