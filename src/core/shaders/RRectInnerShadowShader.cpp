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
#include "gpu/processors/RectInnerShadowFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<Shader> RRectInnerShadowShader::Make(const RRect& shadowRRect,
                                                     const RRect& maskRRect, float sigmaX,
                                                     float sigmaY, const Color& color) {
  if (sigmaX <= 0.0f || sigmaY <= 0.0f || !FloatsAreFinite(&sigmaX, 1) ||
      !FloatsAreFinite(&sigmaY, 1)) {
    return nullptr;
  }
  if (shadowRRect.rect().isEmpty() || maskRRect.rect().isEmpty()) {
    return nullptr;
  }
  DEBUG_ASSERT(!shadowRRect.isComplex() && !maskRRect.isComplex());
  auto shader = std::make_shared<RRectInnerShadowShader>(shadowRRect.rect(), shadowRRect.radii()[0],
                                                         maskRRect.rect(), maskRRect.radii()[0],
                                                         sigmaX, sigmaY, color);
  shader->weakThis = shader;
  return shader;
}

RRectInnerShadowShader::RRectInnerShadowShader(const Rect& shadowRect, const Point& shadowRadius,
                                               const Rect& maskRect, const Point& maskRadius,
                                               float sigmaX, float sigmaY, const Color& color)
    : shadowRect(shadowRect), shadowRadius(shadowRadius), maskRect(maskRect),
      maskRadius(maskRadius), sigmaX(sigmaX), sigmaY(sigmaY), color(color) {
}

bool RRectInnerShadowShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::RRectInnerShadow) {
    return false;
  }
  auto other = static_cast<const RRectInnerShadowShader*>(shader);
  return shadowRect == other->shadowRect && shadowRadius == other->shadowRadius &&
         maskRect == other->maskRect && maskRadius == other->maskRadius &&
         sigmaX == other->sigmaX && sigmaY == other->sigmaY && color == other->color;
}

PlacementPtr<FragmentProcessor> RRectInnerShadowShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  const auto shadowCenter = Point::Make(shadowRect.centerX(), shadowRect.centerY());
  auto shadowCoordMatrix = Matrix::MakeTrans(-shadowCenter.x, -shadowCenter.y);
  if (uvMatrix != nullptr) {
    shadowCoordMatrix.preConcat(*uvMatrix);
  }
  auto maskCoordMatrix = shadowCoordMatrix;
  maskCoordMatrix.postTranslate(shadowCenter.x - maskRect.centerX(),
                                shadowCenter.y - maskRect.centerY());
  maskCoordMatrix.postScale(1.0f / sigmaX, 1.0f / sigmaY);

  const auto maskHalfOverSigma =
      Point::Make(maskRect.width() * 0.5f / sigmaX, maskRect.height() * 0.5f / sigmaY);
  const auto shadowHalfSize = Point::Make(shadowRect.width() * 0.5f, shadowRect.height() * 0.5f);
  const auto pmColor = ToPMColor(color, dstColorSpace);
  auto allocator = args.context->drawingAllocator();
  // Near-zero radii degenerate to the rectangle form, which the rect processor evaluates without
  // the quadrature.
  if (FloatNearlyZero(shadowRadius.x) && FloatNearlyZero(shadowRadius.y) &&
      FloatNearlyZero(maskRadius.x) && FloatNearlyZero(maskRadius.y)) {
    return RectInnerShadowFragmentProcessor::Make(allocator, maskHalfOverSigma, shadowHalfSize,
                                                  pmColor, shadowCoordMatrix, maskCoordMatrix);
  }
  const auto maskCornerOverSigma = Point::Make(maskRadius.x / sigmaX, maskRadius.y / sigmaY);
  return RRectInnerShadowFragmentProcessor::Make(allocator, maskHalfOverSigma, maskCornerOverSigma,
                                                 shadowHalfSize, shadowRadius, pmColor,
                                                 shadowCoordMatrix, maskCoordMatrix);
}
}  // namespace tgfx
