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

#include "gpu/aot/write/TextureList.h"
#include <cstring>
#include "core/utils/Log.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/Layout.h"
#include "gpu/shaders/Module.h"

namespace tgfx {

namespace {

/// Whether a declared slot name belongs to the module: <Module>_<element>, or the bare element name
/// for a bare-names module.
bool ownsSlot(const Module& module, const std::string& name) {
  if (module.bareNames) {
    return true;
  }
  const size_t length = std::strlen(module.name);
  return name.size() > length && name[length] == '_' && name.compare(0, length, module.name) == 0;
}

}  // namespace

TextureList::TextureList(const Layout& layout) : layout(layout) {
  entries.reserve(layout.textures.size());
}

bool TextureList::nextBelongsTo(const Module& module) {
  if (entries.size() >= layout.textures.size()) {
    LOGE("TextureList: %s overflows the %zu declared slots", module.name, layout.textures.size());
    return false;
  }
  const auto& declared = layout.textures[entries.size()];
  // The one check that replaces the hole-position guess: the slot is filled by the module the
  // declaration named, not by whichever source happened to come next. Compared without composing
  // a prefix string: this runs once per texture slot of every draw.
  const bool owns = ownsSlot(module, declared.name);
  if (!owns) {
    LOGE("TextureList: slot %zu is %s, the writer filled it for %s", entries.size(),
         declared.name.c_str(), module.name);
    DEBUG_ASSERT(false);
    return false;
  }
  return true;
}

bool TextureList::nextIsFor(const Module& module) const {
  if (entries.size() >= layout.textures.size()) {
    return false;
  }
  return ownsSlot(module, layout.textures[entries.size()].name);
}

bool TextureList::pushFor(const Module& module, std::shared_ptr<Texture> texture,
                          const SamplerState& state) {
  if (!nextBelongsTo(module)) {
    return false;
  }
  entries.push_back({std::move(texture), state});
  return true;
}

bool TextureList::pushPlaceholderFor(const Module& module) {
  if (!nextBelongsTo(module)) {
    return false;
  }
  entries.push_back({nullptr, {}});
  return true;
}

bool TextureList::pushPlanesFor(const Module& module, const YUVTextureView& view,
                                const SamplerState& state) {
  for (size_t index = 0; index < view.textureCount(); ++index) {
    if (!pushFor(module, view.getTextureAt(index), state)) {
      return false;
    }
  }
  return true;
}

bool TextureList::pushProxyFor(const Module& module, const TextureProxy* proxy,
                               const SamplerState& state) {
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  if (view == nullptr || view->getTexture() == nullptr) {
    return pushPlaceholderFor(module);
  }
  return pushFor(module, view->getTexture(), state);
}

bool TextureList::complete() const {
  return entries.size() == layout.textures.size();
}

}  // namespace tgfx
