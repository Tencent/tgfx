/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TGFXView.h"

namespace hello2d {
class TGFXLayerView : public TGFXView {
 public:
  TGFXLayerView(std::string canvasID, const emscripten::val& nativeImage);

  void setTreeName(const std::string& treeName) {
    _treeName = treeName;
  }

  void hitTest(float x, float y);

 protected:
  bool onDraw(tgfx::Surface* surface, const drawers::AppHost* appHost, int drawIndex) override;

  std::string _treeName;
};
}  // namespace hello2d