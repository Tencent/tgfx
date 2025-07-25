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

}  // namespace tgfx
