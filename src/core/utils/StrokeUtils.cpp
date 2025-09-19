#include "StrokeUtils.h"
#include <cmath>

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

bool TreatStrokeAsHairline(const Stroke& stroke, const Matrix& matrix) {
  if (stroke.isHairline()) {
    return false;
  }
  auto maxWidth = stroke.width * matrix.getMaxScale();
  return maxWidth < 1.f;
}
}  // namespace tgfx
