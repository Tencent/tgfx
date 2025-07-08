#include <cmath>
#include "ApplyStrokeToBounds.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

void ApplyStrokeToBounds(const Stroke& stroke, Rect* bounds, bool applyMiterLimit) {
  if (bounds == nullptr) {
    return;
  }
  auto expand = stroke.width * 0.5f;
  if (applyMiterLimit && stroke.join == LineJoin::Miter) {
    expand *= stroke.miterLimit;
  }
  expand = ceilf(expand);
  bounds->outset(expand, expand);
}

void ApplyStrokeToScaledBounds(const Stroke& stroke, Rect* bounds, float resolutionScale,
                               bool applyMiterLimit) {
  if (FloatNearlyEqual(resolutionScale, 1.0f)) {
    ApplyStrokeToBounds(stroke, bounds, applyMiterLimit);
    return;
  }
  if (bounds == nullptr || FloatNearlyZero(resolutionScale)) {
    return;
  }
  bounds->scale(1.0f / resolutionScale, 1.0f / resolutionScale);
  ApplyStrokeToBounds(stroke, bounds, applyMiterLimit);
  bounds->scale(resolutionScale, resolutionScale);
}

}  // namespace tgfx
