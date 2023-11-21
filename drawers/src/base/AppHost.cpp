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

#include "tdraw/AppHost.h"
#include "tgfx/platform/Print.h"

namespace tdraw {
AppHost::AppHost(int width, int height, float density)
    : _width(width), _height(height), _density(density) {
}

std::shared_ptr<tgfx::Image> AppHost::getImage(const std::string& name) const {
  auto result = images.find(name);
  if (result != images.end()) {
    return result->second;
  }
  return nullptr;
}

std::shared_ptr<tgfx::Typeface> AppHost::getTypeface(const std::string& name) const {
  auto result = typefaces.find(name);
  if (result != typefaces.end()) {
    return result->second;
  }
  return nullptr;
}

void AppHost::updateScreen(int width, int height, float density) {
  if (width <= 0 || height <= 0) {
    tgfx::PrintError("AppContext::updateScreen() width or height is invalid!");
    return;
  }
  if (density <= 1.0) {
    tgfx::PrintError("AppContext::updateScreen() density is invalid!");
    return;
  }
  _width = width;
  _height = height;
  _density = density;
}

void AppHost::addImage(const std::string& name, std::shared_ptr<tgfx::Image> image) {
  if (name.empty()) {
    tgfx::PrintError("AppContext::setImage() name is empty!");
    return;
  }
  if (image == nullptr) {
    tgfx::PrintError("AppContext::setImage() image is nullptr!");
    return;
  }
  if (images.count(name) > 0) {
    tgfx::PrintError("AppContext::setImage() image with name %s already exists!", name.c_str());
    return;
  }
  images[name] = std::move(image);
}

void AppHost::addTypeface(const std::string& name, std::shared_ptr<tgfx::Typeface> typeface) {
  if (name.empty()) {
    tgfx::PrintError("AppContext::setTypeface() name is empty!");
    return;
  }
  if (typeface == nullptr) {
    tgfx::PrintError("AppContext::setTypeface() typeface is nullptr!");
    return;
  }
  if (typefaces.count(name) > 0) {
    tgfx::PrintError("AppContext::setTypeface() typeface with name %s already exists!",
                     name.c_str());
    return;
  }
  typefaces[name] = std::move(typeface);
}
}  // namespace tdraw