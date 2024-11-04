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

class LayerTreeDrawer : public Drawer {
 public:
  LayerTreeDrawer(const std::string& treeName);

  bool isLayerTreeDrawer() const override {
    return true;
  }

  /*
   *  click the layer tree at the given point.
   *  @return A list of layers that were clicked.
   */
  std::vector<std::shared_ptr<tgfx::Layer>> click(float x, float y);

  /*
   *  Renders the layer tree onto the given surface.
   *  @return True if the surface content was updated, otherwise false.
   */
  bool render(tgfx::Surface* surface, const AppHost* host);

 protected:
  virtual std::shared_ptr<tgfx::Layer> buildLayerTree(const AppHost* host) = 0;

  virtual void onClick(float x, float y);

  void onDraw(tgfx::Canvas* canvas, const AppHost* host) override;

  bool renderInternal(tgfx::Surface* surface, const AppHost* host, bool replaceAll);

 private:
  void updateRootMatrix(const AppHost* host);

  // use to updateMatrix
  std::shared_ptr<tgfx::Layer> root;

  tgfx::DisplayList displayList;
};

}  // namespace drawers
