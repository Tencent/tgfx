/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/bind/Binder.h"
#include <array>
#include <utility>

namespace tgfx {
namespace {

// The subset semantics below derive the fields from the binding's own data, the same way the
// module bind's field writes do; byte-for-byte parity is what lets the reader take the chain's
// children over.
bool BindingNeedsSubset(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                        const std::optional<Rect>& subset) {
  auto bounds = Rect::MakeWH(proxy->width(), proxy->height());
  if (subset.has_value() && !(*subset).contains(bounds)) {
    return true;
  }
  if (proxy->width() != view->width() || proxy->height() != view->height()) {
    return true;
  }
  return view->getTexture()->type() == TextureType::External;
}

// The sampler kinds the readers carry (SamplerState on the IR leaf, SamplingOptions on a chain
// binding) both spell the filter modes the same way, so one template serves both.
template <typename Sampling>
void LeafSubsetRect(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                    const std::optional<Rect>& subset, const Sampling& sampling, float rect[4]) {
  auto subsetRect = subset.value_or(Rect::MakeWH(proxy->width(), proxy->height()));
  if (BindingNeedsSubset(proxy, view, subset)) {
    if (sampling.magFilterMode == sampling.minFilterMode &&
        sampling.magFilterMode == FilterMode::Nearest) {
      subsetRect.roundOut();
    }
    auto type = view->getTexture()->type();
    // Normally this would just need to take 1/2 a texel off each end, but because the chroma
    // channels of YUV420 images are subsampled we may need to shrink the crop region by a whole
    // texel on each side for external textures.
    auto inset = type == TextureType::External ? 1.0f : 0.5f;
    subsetRect = subsetRect.makeInset(inset, inset);
  }
  rect[0] = subsetRect.left;
  rect[1] = subsetRect.top;
  rect[2] = subsetRect.right;
  rect[3] = subsetRect.bottom;
  if (view->origin() == ImageOrigin::BottomLeft) {
    auto height = static_cast<float>(view->height());
    rect[1] = height - rect[1];
    rect[3] = height - rect[3];
    std::swap(rect[1], rect[3]);
  }
  auto type = view->getTexture()->type();
  if (type != TextureType::Rectangle) {
    auto lt = view->getTextureCoord(rect[0], rect[1]);
    auto rb = view->getTextureCoord(rect[2], rect[3]);
    rect[0] = lt.x;
    rect[1] = lt.y;
    rect[2] = rb.x;
    rect[3] = rb.y;
  }
}

}  // namespace

/// The declared-family writer reads the same subset facts the module binds compute; the helpers
/// above stay file-local, so these wrappers export them with the IR leaf's SamplerState.
bool LeafSubsetClampOn(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                       const std::optional<Rect>& subset) {
  return BindingNeedsSubset(proxy, view, subset);
}

void LeafSubsetRectForSampler(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                              const std::optional<Rect>& subset, const SamplerState& sampling,
                              float rect[4]) {
  LeafSubsetRect(proxy, view, subset, sampling, rect);
}

Matrix DeviceMaskCoordMatrix(const Matrix& uvMatrix, const TextureView& view) {
  const auto scale = view.getTextureCoord(1, 1);
  Matrix matrix = uvMatrix;
  matrix.postScale(scale.x, scale.y);
  return matrix;
}

}  // namespace tgfx
