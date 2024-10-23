

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

void LayerFilter::invalidate() {
  dirty = true;
}

std::shared_ptr<ImageFilter> LayerFilter::getImageFilter(float filterScale) {
  if (dirty || lastScale != filterScale) {
    lastFilter = onCreateImageFilter(filterScale);
    lastScale = filterScale;
    dirty = false;
  }
  return lastFilter;
}

}  // namespace tgfx