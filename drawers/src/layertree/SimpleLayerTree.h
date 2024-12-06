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

#pragma once

#include "base/LayerTreeDrawer.h"

namespace drawers {

class SimpleLayerTree : public drawers::LayerTreeDrawer {
 public:
  SimpleLayerTree() : LayerTreeDrawer("SimpleLayerTree") {
  }

 protected:
  std::shared_ptr<tgfx::Layer> buildLayerTree(const AppHost* host) override;
};

}  // namespace drawers
