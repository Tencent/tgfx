/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "drawers/Drawer.h"
#include "tgfx/layers/DisplayList.h"

#pragma once

namespace drawers {

class CustomLayer;

class LayerDrawer : public Drawer {
 public:
  LayerDrawer(const std::string& treeName);

  std::vector<std::shared_ptr<tgfx::Layer>> getLayersUnderPoint(float x, float y) const;

 protected:
  virtual std::shared_ptr<tgfx::Layer> buildLayerTree(const AppHost* host) = 0;

  virtual void prepare(const AppHost* host) = 0;

  void onDraw(tgfx::Canvas* canvas, const AppHost* host) override;

 private:
  void updateRootMatrix(const AppHost* host);

  // use to updateMatrix
  std::shared_ptr<tgfx::Layer> root;

  tgfx::DisplayList displayList;
};

}  // namespace drawers
