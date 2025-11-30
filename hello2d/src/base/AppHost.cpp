/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "hello2d/AppHost.h"

namespace hello2d {

AppHost::AppHost(int width, int height, float density)
    : _width(width), _height(height), _density(density) {
}

std::shared_ptr<tgfx::Image> AppHost::getImage(const std::string& name) const {
  auto it = images.find(name);
  if (it != images.end()) {
    return it->second;
  }
  return nullptr;
}

std::shared_ptr<tgfx::Typeface> AppHost::getTypeface(const std::string& name) const {
  auto it = typefaces.find(name);
  if (it != typefaces.end()) {
    return it->second;
  }
  return nullptr;
}

bool AppHost::updateScreen(int width, int height, float density) {
  if (_width == width && _height == height && _density == density) {
    return false;
  }
  _width = width;
  _height = height;
  _density = density;
  return true;
}

void AppHost::addImage(const std::string& name, std::shared_ptr<tgfx::Image> image) {
  if (name.empty()) {
    return;
  }
  if (image == nullptr) {
    return;
  }
  if (images.count(name) > 0) {
    return;
  }
  images[name] = std::move(image);
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

}  // namespace hello2d
