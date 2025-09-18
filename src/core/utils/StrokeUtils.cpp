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

bool TreatStrokeAsHairline(const Stroke& stroke, const Matrix& matrix, float* width) {
  if (stroke.isHairline()) {
    return false;
  }
  // Point points[2];
  // Point mappedPoints[2];
  // points[0].set(stroke.width, 0);
  // points[1].set(0, stroke.width);
  // matrix.mapPoints(mappedPoints, points, 2);
  // auto width1 = mappedPoints[0].length();
  // auto width2 = mappedPoints[1].length();
  // auto maxWidth = std::max(width1, width2);
  auto maxWidth = stroke.width * matrix.getMaxScale();
  if (width) {
    *width = maxWidth;
  }
  return maxWidth < 1.f;
}

}  // namespace tgfx
