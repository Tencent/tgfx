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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "gpu/SamplerState.h"

namespace tgfx {

struct Layout;
struct Module;
class Texture;
class TextureProxy;
class YUVTextureView;

/// One slot of the draw's texture bindings. A null texture means the draw supplies nothing there:
/// the binder fills the slot with the shared dummy, so the slots after it keep their positions.
struct BoundTexture {
  std::shared_ptr<Texture> texture = nullptr;
  SamplerState state = {};
};

/**
 * The draw's texture bindings, accumulated in the derived layout's order. The layout is the only
 * ordering authority — slot i is always layout.textures[i] — so a module fills the slots it
 * declares and never reasons about the modules around it.
 */
class TextureList {
 public:
  explicit TextureList(const Layout& layout);

  /// Fills the next slot for `module`, which the declaration must have named for it. A mismatch
  /// means a module's slot order and its provision order drifted, so it is reported and refused.
  bool pushFor(const Module& module, std::shared_ptr<Texture> texture, const SamplerState& state);

  /// Fills the next slot with the shared dummy: a declared texture this draw does not supply.
  bool pushPlaceholderFor(const Module& module);

  /// Fills the next slots with one multi-plane source's planes, in declaration order.
  bool pushPlanesFor(const Module& module, const YUVTextureView& view, const SamplerState& state);

  /// Fills the next slot from a proxy whose view may not have instantiated yet: an absent view
  /// occupies the slot with the dummy rather than shifting what follows.
  bool pushProxyFor(const Module& module, const TextureProxy* proxy, const SamplerState& state);

  /// Whether the next declared slot belongs to `module`: a module with an optional trailing
  /// texture (the glass edge mask) asks before filling it.
  bool nextIsFor(const Module& module) const;

  size_t size() const {
    return entries.size();
  }

  /// Every declared slot was filled.
  bool complete() const;

  const std::vector<BoundTexture>& bindings() const {
    return entries;
  }

 private:
  bool nextBelongsTo(const Module& module);

  const Layout& layout;
  std::vector<BoundTexture> entries = {};
};

}  // namespace tgfx
