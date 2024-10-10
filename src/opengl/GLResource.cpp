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

#include "tgfx/opengl/GLResource.h"
#include "gpu/Resource.h"

namespace tgfx {
class GLExternalResource : public Resource {
 public:
  explicit GLExternalResource(std::shared_ptr<GLResource> glResource)
      : glResource(std::move(glResource)) {
  }

  size_t memoryUsage() const override {
    return 0;
  }

 private:
  std::shared_ptr<GLResource> glResource = nullptr;

  bool isPurgeable() const override {
    return glResource.use_count() <= 1;
  }

  void onReleaseGPU() override {
    glResource->onReleaseGPU();
    glResource->context = nullptr;
  }
};

void GLResource::AttachToContext(Context* context, std::shared_ptr<GLResource> glResource) {
  if (context == nullptr || glResource == nullptr) {
    return;
  }
  glResource->context = context;
  Resource::AddToCache(context, new GLExternalResource(std::move(glResource)));
}
}  // namespace tgfx
