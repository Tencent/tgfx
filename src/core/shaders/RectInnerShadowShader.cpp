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

#include "core/shaders/RectInnerShadowShader.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "gpu/processors/RectInnerShadowFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<Shader> RectInnerShadowShader::Make(const Rect& maskRect, const Rect& shadowRect,
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
  if (maskRect.isEmpty() || shadowRect.isEmpty()) {
    return nullptr;
  }
  return std::make_shared<RectInnerShadowShader>(maskRect, shadowRect, shadowCenterOffset, sigmaX,
                                                 sigmaY, color, localScale);
}

RectInnerShadowShader::RectInnerShadowShader(const Rect& maskRect, const Rect& shadowRect,
                                             const Point& shadowCenterOffset, float sigmaX,
                                             float sigmaY, const Color& color, float localScale)
    : maskRect(maskRect), shadowRect(shadowRect), shadowCenterOffset(shadowCenterOffset),
      sigmaX(sigmaX), sigmaY(sigmaY), color(color), localScale(localScale) {
}

bool RectInnerShadowShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::RectInnerShadow) {
    return false;
  }
  // Every shadow allocates its own shader, so pointer comparison in CompareBrush always fails and
  // batching depends entirely on this field-by-field check. Comparing less than all of the state
  // would let shadows with different geometry merge into one op and render with the first one's
  // parameters.
  auto other = static_cast<const RectInnerShadowShader*>(shader);
  return maskRect == other->maskRect && shadowRect == other->shadowRect &&
         shadowCenterOffset == other->shadowCenterOffset && sigmaX == other->sigmaX &&
         sigmaY == other->sigmaY && color == other->color && localScale == other->localScale;
}

PlacementPtr<FragmentProcessor> RectInnerShadowShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  const auto maskCenter = Point::Make(maskRect.centerX(), maskRect.centerY());
  // Unlike the outer shadow, the coordinates handed to the processor stay unnormalized: the mask's
  // antialiased edge is one pixel wide regardless of sigma, so it has to be measured in the space
  // the geometry lives in. The processor divides by sigma itself for the shadow term.
  auto totalMatrix = Matrix::MakeScale(localScale, localScale);
  totalMatrix.postTranslate(-maskCenter.x, -maskCenter.y);
  if (uvMatrix != nullptr) {
    totalMatrix.preConcat(*uvMatrix);
  }

  const auto shadowHalfOverSigma =
      Point::Make(shadowRect.width() * 0.5f / sigmaX, shadowRect.height() * 0.5f / sigmaY);
  const auto invSigma = Point::Make(1.0f / sigmaX, 1.0f / sigmaY);
  const auto maskHalfSize = Point::Make(maskRect.width() * 0.5f, maskRect.height() * 0.5f);
  const auto pmColor = ToPMColor(color, dstColorSpace);
  auto allocator = args.context->drawingAllocator();
  return RectInnerShadowFragmentProcessor::Make(allocator, shadowHalfOverSigma, shadowCenterOffset,
                                                invSigma, maskHalfSize, pmColor, &totalMatrix);
}
}  // namespace tgfx
