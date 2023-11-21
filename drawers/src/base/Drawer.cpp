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

#include "tdraw/Drawer.h"
#include <unordered_map>
#include "base/Drawers.h"
#include "tgfx/platform/Print.h"

namespace tdraw {
static std::vector<Drawer*> drawers = {new GridBackground(), new SweepGradient(),
                                       new ImageWithMipmap(), new ImageWithShadow(),
                                       new SimpleText()};

static std::vector<std::string> GetDrawerNames() {
  std::vector<std::string> names;
  for (const auto& drawer : drawers) {
    names.push_back(drawer->name());
  }
  return names;
}

static std::unordered_map<std::string, Drawer*> GetDrawerMap() {
  std::unordered_map<std::string, Drawer*> map;
  for (const auto& drawer : drawers) {
    map[drawer->name()] = drawer;
  }
  return map;
}

int Drawer::Count() {
  return static_cast<int>(drawers.size());
}

const std::vector<std::string>& Drawer::Names() {
  static auto names = GetDrawerNames();
  return names;
}

const Drawer* Drawer::GetByIndex(int index) {
  if (index < 0 || index >= Count()) {
    return nullptr;
  }
  return drawers[index];
}

const Drawer* Drawer::GetByName(const std::string& name) {
  static auto drawerMap = GetDrawerMap();
  auto it = drawerMap.find(name);
  if (it == drawerMap.end()) {
    return nullptr;
  }
  return it->second;
}

Drawer::Drawer(std::string name) : _name(std::move(name)) {
}

void Drawer::draw(tgfx::Canvas* canvas, const AppHost* host) const {
  if (canvas == nullptr) {
    tgfx::PrintError("Drawer::draw() canvas is nullptr!");
    return;
  }
  if (host == nullptr) {
    tgfx::PrintError("Drawer::draw() context is nullptr!");
    return;
  }
  canvas->save();
  onDraw(canvas, host);
  canvas->restore();
}
}  // namespace tdraw