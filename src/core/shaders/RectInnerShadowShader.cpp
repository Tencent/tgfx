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

std::shared_ptr<Shader> RectInnerShadowShader::Make(const Rect& shadowRect, const Rect& maskRect,
                                                    float sigmaX, float sigmaY,
                                                    const Color& color) {
  if (sigmaX <= 0.0f || sigmaY <= 0.0f || !FloatsAreFinite(&sigmaX, 1) ||
      !FloatsAreFinite(&sigmaY, 1)) {
    return nullptr;
  }
  if (shadowRect.isEmpty() || maskRect.isEmpty()) {
    return nullptr;
  }
  auto shader =
      std::make_shared<RectInnerShadowShader>(shadowRect, maskRect, sigmaX, sigmaY, color);
  shader->weakThis = shader;
  return shader;
}

RectInnerShadowShader::RectInnerShadowShader(const Rect& shadowRect, const Rect& maskRect,
                                             float sigmaX, float sigmaY, const Color& color)
    : shadowRect(shadowRect), maskRect(maskRect), sigmaX(sigmaX), sigmaY(sigmaY), color(color) {
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
  return shadowRect == other->shadowRect && maskRect == other->maskRect &&
         sigmaX == other->sigmaX && sigmaY == other->sigmaY && color == other->color;
}

PlacementPtr<FragmentProcessor> RectInnerShadowShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  const auto shadowCenter = Point::Make(shadowRect.centerX(), shadowRect.centerY());
  // Unlike the outer shadow, the shadow coordinates stay unnormalized: the shadow shape's
  // antialiased edge is one pixel wide regardless of sigma, so it has to be measured in the space
  // the geometry lives in. The mask term gets its own sigma-normalized space instead.
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
  return RectInnerShadowFragmentProcessor::Make(allocator, maskHalfOverSigma, shadowHalfSize,
                                                pmColor, shadowCoordMatrix, maskCoordMatrix);
}
}  // namespace tgfx
