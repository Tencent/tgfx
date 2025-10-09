/////////////////////////////////////////////////////////////////////////////////////////////////
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

#pragma once
#include "AppHost.h"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"
namespace inspector {
class TextureDrawer : public QQuickItem {
  Q_OBJECT
 public:
  TextureDrawer(QQuickItem* parent = nullptr);
  ~TextureDrawer() = default;

  Q_SLOT void onSelectedImage(std::shared_ptr<tgfx::Image> image);

 protected:
  void draw();
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

 private:
  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;
  std::shared_ptr<tgfx::Image> image;
};
}  // namespace inspector