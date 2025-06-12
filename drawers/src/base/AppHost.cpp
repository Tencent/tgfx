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

#include "drawers/AppHost.h"
#include "tgfx/platform/Print.h"

namespace drawers {
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

bool AppHost::updateScreen(int width, int height, float density) {
  if (width <= 0 || height <= 0) {
    tgfx::PrintError("AppHost::updateScreen() width or height is invalid!");
    return false;
  }
  if (density < 1.0) {
    tgfx::PrintError("AppHost::updateScreen() density is invalid!");
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

bool AppHost::updateZoomAndOffset(float zoomScale, const tgfx::Point& contentOffset) {
  if (zoomScale == _zoomScale && contentOffset == _contentOffset) {
    return false;
  }
  _zoomScale = zoomScale;
  _contentOffset = contentOffset;
  return true;
}

void AppHost::addImage(const std::string& name, std::shared_ptr<tgfx::Image> image) {
  if (name.empty()) {
    tgfx::PrintError("AppHost::addImage() name is empty!");
    return;
  }
  if (image == nullptr) {
    tgfx::PrintError("AppHost::addImage() image is nullptr!");
    return;
  }
  if (images.count(name) > 0) {
    tgfx::PrintError("AppHost::addImage() image with name %s already exists!", name.c_str());
    return;
  }
  images[name] = std::move(image);
}

void AppHost::addTypeface(const std::string& name, std::shared_ptr<tgfx::Typeface> typeface) {
  if (name.empty()) {
    tgfx::PrintError("AppHost::addTypeface() name is empty!");
    return;
  }
  if (typeface == nullptr) {
    tgfx::PrintError("AppHost::addTypeface() typeface is nullptr!");
    return;
  }
  if (typefaces.count(name) > 0) {
    tgfx::PrintError("AppHost::addTypeface() typeface with name %s already exists!", name.c_str());
    return;
  }
  typefaces[name] = std::move(typeface);
}
}  // namespace drawers
