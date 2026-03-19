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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "base/LayerBuilders.h"
#include "layers/utils/LayerTreeParser.h"
#include "tgfx/layers/VectorLayer.h"
#include "tgfx/platform/Print.h"

namespace hello2d {

static std::string GetLayerTreeJsonPath() {
  auto dir = std::string(__FILE__).substr(0, std::string(__FILE__).rfind('/'));
  // Use complex test case for debugging
  return dir + "/layer_tree.json";
}

static void DumpLayerDebug(const std::shared_ptr<tgfx::Layer>& layer, int depth) {
  if (!layer) return;
  auto indent = std::string(static_cast<size_t>(depth) * 2, ' ');
  auto bounds = layer->getBounds();
  auto boundsEmpty = (bounds.width() <= 0 || bounds.height() <= 0);
  tgfx::PrintLog(
      "%s[%d] type=%d name='%s' visible=%d alpha=%.2f bounds=[%.1f,%.1f,%.1f,%.1f]%s "
      "children=%zu",
      indent.c_str(), static_cast<int>(layer->type()), static_cast<int>(layer->type()),
      layer->name().c_str(), layer->visible(), layer->alpha(), bounds.left, bounds.top,
      bounds.right, bounds.bottom, boundsEmpty ? " <<EMPTY>>" : "", layer->children().size());
  if (layer->type() == tgfx::LayerType::Vector) {
    auto vector = std::static_pointer_cast<tgfx::VectorLayer>(layer);
    tgfx::PrintLog("%s  -> VectorLayer contents count: %zu", indent.c_str(),
                   vector->contents().size());
  }
  if (layer->mask()) {
    tgfx::PrintLog("%s  -> has mask: name='%s'", indent.c_str(), layer->mask()->name().c_str());
  }
  auto styles = layer->layerStyles();
  if (!styles.empty()) {
    tgfx::PrintLog("%s  -> layerStyles count: %zu", indent.c_str(), styles.size());
  }
  for (const auto& child : layer->children()) {
    DumpLayerDebug(child, depth + 1);
  }
}

std::shared_ptr<tgfx::Layer> Layer3DTree::onBuildLayerTree(const AppHost*) {
  auto jsonPath = GetLayerTreeJsonPath();
  tgfx::PrintLog("=== Layer3DTree: loading JSON from: %s ===", jsonPath.c_str());
  auto root = tgfx::LayerTreeParser::ParseTree(jsonPath);
  if (!root) {
    tgfx::PrintLog("=== Layer3DTree: ParseTree returned nullptr! ===");
    return nullptr;
  }
  tgfx::PrintLog("=== Layer3DTree: parsed tree structure ===");
  DumpLayerDebug(root, 0);
  tgfx::PrintLog("=== Layer3DTree: end of tree dump ===");
  return root;
}
}  // namespace hello2d
