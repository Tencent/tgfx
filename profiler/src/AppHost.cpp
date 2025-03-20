////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "AppHost.h"

AppHost::AppHost(int width, int height, float density)
    : _width(width), _height(height), _density(density) {
}

void AppHost::addTypeface(const std::string& name, std::shared_ptr<tgfx::Typeface> typeface) {
  if (name.empty()) {
    return;
  }
  if (typeface == nullptr) {
    return;
  }
  if (typefaces.count(name) > 0) {
    return;
  }
  typefaces[name] = std::move(typeface);
}

std::shared_ptr<tgfx::Typeface> AppHost::getTypeface(const std::string& name) const {
  auto result = typefaces.find(name);
  if (result != typefaces.end()) {
    return result->second;
  }
  return nullptr;
}

bool AppHost::updateScreen(int width, int height, float density) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (density < 1.0) {
    return false;
  }
  if (width == _width && height == _height && density == _density) {
    return false;
  }
  _width = width;
  _height = height;
  _density = density;
  return true;
}

std::shared_ptr<AppHost> AppHostInstance::GetAppHostInstance() {
  static std::shared_ptr<AppHost> appHost;
  if (appHost) {
    return appHost;
  }

  appHost = std::make_unique<AppHost>();
#ifdef __APPLE__
  auto defaultTypeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
#else
  auto defaultTypeface = tgfx::Typeface::MakeFromName("Microsoft YaHei", "");
#endif
  appHost->addTypeface("default", defaultTypeface);
  return appHost;
}
