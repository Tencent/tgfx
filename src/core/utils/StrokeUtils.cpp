#include "StrokeUtils.h"
#include <cmath>
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

bool IsHairlineStroke(const Stroke& stroke) {
  return stroke.width <= 0 || FloatNearlyZero(stroke.width);
}

bool TreatStrokeAsHairline(const Stroke& stroke, const Matrix& matrix) {
  if (IsHairlineStroke(stroke)) {
    return true;
  }
  if (!FloatNearlyEqual(matrix.getScaleX(), matrix.getScaleY())) {
    return false;
  }
  auto maxWidth = stroke.width * matrix.getMaxScale();
  return maxWidth < 1.f;
}

float GetHairlineAlphaFactor(const Stroke& stroke, const Matrix& matrix) {
  if (IsHairlineStroke(stroke)) {
    return 1.0f;
  }
  auto scaledStrokeWidth = stroke.width * matrix.getMaxScale();
  if (scaledStrokeWidth < 1.0f) {
    return scaledStrokeWidth;
  }
  return 1.0f;
}

std::vector<float> SimplifyLineDashPattern(const std::vector<float>& pattern,
                                           const Stroke& stroke) {
  // When LineCap is Square, the endpoints extend by half the line width.
  // If an unpainted dash segment is less than or equal to the line width, the painted segments will
  // connect seamlessly. Therefore, such a dash segment can be omitted for simplification.
  if (stroke.cap != LineCap::Square) {
    return pattern;
  }
  float addedPaintLength = 0.0f;
  std::vector<float> simplifiedDashes = {};
  for (uint32_t i = 0; i < pattern.size(); i += 2) {
    auto paintedLength = pattern[i];
    auto unpaintedLength = pattern[i + 1];
    if (unpaintedLength <= stroke.width) {
      addedPaintLength += paintedLength + unpaintedLength;
    } else {
      simplifiedDashes.push_back(paintedLength + addedPaintLength);
      simplifiedDashes.push_back(unpaintedLength);
      addedPaintLength = 0.0f;
    }
  }
  return simplifiedDashes;
}

}  // namespace tgfx
