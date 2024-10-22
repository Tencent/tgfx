

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

void LayerFilter::invalidate() {
  dirty = true;
}

Rect LayerFilter::filterBounds(const Rect& bounds, float filterScale) {
  auto filter = getImageFilter(filterScale);
  if (!filter) {
    return Rect::MakeEmpty();
  }

  auto result = filter->filterBounds(bounds);
  if (_clipBounds) {
    auto clipRect = *_clipBounds;
    clipRect.scale(filterScale, filterScale);
    if (!result.intersect(clipRect)) {
      return Rect::MakeEmpty();
    }
  }
  return result;
}

std::shared_ptr<Image> LayerFilter::applyFilter(const std::shared_ptr<Image>& image,
                                                float filterScale, Point* offset) {
  auto filter = getImageFilter(filterScale);
  if (!filter) {
    return image;
  }
  if (_clipBounds) {
    auto clipRect = *_clipBounds;
    clipRect.scale(filterScale, filterScale);
    return image->makeWithFilter(filter, offset, &clipRect);
  } else {
    return image->makeWithFilter(filter, offset);
  }
}

void LayerFilter::setClipBounds(const Rect& clipBounds) {
  if ((_clipBounds && *_clipBounds == clipBounds) || (!_clipBounds && clipBounds.isEmpty())) {
    return;
  }
  if (clipBounds.isEmpty()) {
    _clipBounds = nullptr;
  } else {
    _clipBounds = std::make_unique<Rect>(clipBounds);
  }
  invalidate();
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