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

#include "core/shaders/RRectBlurShader.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "gpu/processors/RRectBlurFragmentProcessor.h"
#include "gpu/processors/RectBlurFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<Shader> RRectBlurShader::Make(const RRect& rRect, float sigmaX, float sigmaY,
                                              const Color& color) {
  if (sigmaX <= 0.0f || sigmaY <= 0.0f || !FloatsAreFinite(&sigmaX, 1) ||
      !FloatsAreFinite(&sigmaY, 1)) {
    return nullptr;
  }
  if (rRect.rect().isEmpty()) {
    return nullptr;
  }
  DEBUG_ASSERT(!rRect.isComplex());
  auto shader =
      std::make_shared<RRectBlurShader>(rRect.rect(), rRect.radii()[0], sigmaX, sigmaY, color);
  shader->weakThis = shader;
  return shader;
}

RRectBlurShader::RRectBlurShader(const Rect& rect, const Point& radius, float sigmaX, float sigmaY,
                                 const Color& color)
    : rect(rect), radius(radius), sigmaX(sigmaX), sigmaY(sigmaY), color(color) {
}

bool RRectBlurShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::RRectBlur) {
    return false;
  }
  auto other = static_cast<const RRectBlurShader*>(shader);
  return rect == other->rect && radius == other->radius && sigmaX == other->sigmaX &&
         sigmaY == other->sigmaY && color == other->color;
}

PlacementPtr<FragmentProcessor> RRectBlurShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  const auto center = Point::Make(rect.centerX(), rect.centerY());
  auto totalMatrix = Matrix::MakeTrans(-center.x, -center.y);
  totalMatrix.postScale(1.0f / sigmaX, 1.0f / sigmaY);
  if (uvMatrix != nullptr) {
    totalMatrix.preConcat(*uvMatrix);
  }

  const auto halfSizeOverSigma =
      Point::Make(rect.width() * 0.5f / sigmaX, rect.height() * 0.5f / sigmaY);
  const auto pmColor = ToPMColor(color, dstColorSpace);
  auto allocator = args.context->drawingAllocator();
  // A near-zero radius degenerates to the rectangle form, which the rect processor evaluates
  // without the quadrature.
  if (FloatNearlyZero(radius.x) && FloatNearlyZero(radius.y)) {
    return RectBlurFragmentProcessor::Make(allocator, halfSizeOverSigma, pmColor, totalMatrix);
  }
  const auto cornerOverSigma = Point::Make(radius.x / sigmaX, radius.y / sigmaY);
  return RRectBlurFragmentProcessor::Make(allocator, halfSizeOverSigma, cornerOverSigma, pmColor,
                                          totalMatrix);
}
}  // namespace tgfx
