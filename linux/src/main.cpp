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

#include <filesystem>
#include <fstream>
#include "drawers/Drawer.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/GLDevice.h"
#include "tgfx/platform/Print.h"

static std::string GetRootPath() {
  std::filesystem::path filePath = __FILE__;
  auto dir = filePath.parent_path().string();
  return std::filesystem::path(dir + "/../..").lexically_normal();
}

static void SaveFile(std::shared_ptr<tgfx::Data> data, const std::string& output) {
  std::filesystem::path path = output;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  out.write(reinterpret_cast<const char*>(data->data()),
            static_cast<std::streamsize>(data->size()));
  out.close();
}

int main() {
  auto rootPath = GetRootPath();
  drawers::AppHost appHost(720, 720, 2.0f);
  auto image = tgfx::Image::MakeFromFile(rootPath + "resources/assets/bridge.jpg");
  appHost.addImage("bridge", std::move(image));
  auto typeface = tgfx::Typeface::MakeFromPath(rootPath + "resources/font/NotoSansSC-Regular.otf");
  appHost.addTypeface("default", std::move(typeface));
  typeface = tgfx::Typeface::MakeFromPath(rootPath + "resources/font/NotoColorEmoji.ttf");
  appHost.addTypeface("emoji", std::move(typeface));

  auto device = tgfx::GLDevice::Make();
  if (device == nullptr) {
    tgfx::PrintError("Failed to create the Device!");
    return -1;
  }
  auto context = device->lockContext();
  if (context == nullptr) {
    tgfx::PrintError("Failed to lock the Context!");
    return -1;
  }
  auto surface = tgfx::Surface::Make(context, appHost.width(), appHost.height());
  auto canvas = surface->getCanvas();
  auto drawerNames = drawers::Drawer::Names();
  for (auto& name : drawerNames) {
    auto drawer = drawers::Drawer::GetByName(name);
    canvas->clear();
    drawer->draw(canvas, &appHost);
    tgfx::Bitmap bitmap = {};
    bitmap.allocPixels(surface->width(), surface->height());
    auto pixels = bitmap.lockPixels();
    auto success = surface->readPixels(bitmap.info(), pixels);
    bitmap.unlockPixels();
    if (!success) {
      tgfx::PrintError("Failed to readPixels!");
      return -1;
    }
    auto data = bitmap.encode();
    if (data == nullptr) {
      tgfx::PrintError("Failed to encode bitmap!");
      return -1;
    }
    SaveFile(data, "out/" + name + ".png");
  }
  device->unlock();
  tgfx::PrintLog("All images have been saved to the 'out/' directory");
  return 0;
}
