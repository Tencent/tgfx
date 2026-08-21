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

#include "core/shaders/RectBlurShader.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "gpu/processors/RectBlurFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<Shader> RectBlurShader::Make(const Rect& rect, float sigmaX, float sigmaY,
                                             const Color& color, float localScale) {
  if (sigmaX <= 0.0f || sigmaY <= 0.0f || !FloatsAreFinite(&sigmaX, 1) ||
      !FloatsAreFinite(&sigmaY, 1)) {
    return nullptr;
  }
  if (localScale <= 0.0f) {
    return nullptr;
  }
  if (rect.isEmpty()) {
    return nullptr;
  }
  return std::make_shared<RectBlurShader>(rect, sigmaX, sigmaY, color, localScale);
}

RectBlurShader::RectBlurShader(const Rect& rect, float sigmaX, float sigmaY, const Color& color,
                               float localScale)
    : rect(rect), sigmaX(sigmaX), sigmaY(sigmaY), color(color), localScale(localScale) {
}

bool RectBlurShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::RectBlur) {
    return false;
  }
  // Every shadow allocates its own shader, so pointer comparison in CompareBrush always fails and
  // batching depends entirely on this field-by-field check. Comparing less than all of the state
  // would let shadows with different geometry merge into one op and render with the first one's
  // parameters.
  auto other = static_cast<const RectBlurShader*>(shader);
  return rect == other->rect && sigmaX == other->sigmaX && sigmaY == other->sigmaY &&
         color == other->color && localScale == other->localScale;
}

PlacementPtr<FragmentProcessor> RectBlurShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  const auto center = Point::Make(rect.centerX(), rect.centerY());
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
      Point::Make(rect.width() * 0.5f / sigmaX, rect.height() * 0.5f / sigmaY);
  const auto pmColor = ToPMColor(color, dstColorSpace);
  auto allocator = args.context->drawingAllocator();
  return RectBlurFragmentProcessor::Make(allocator, halfSizeOverSigma, pmColor, &totalMatrix);
}
}  // namespace tgfx
