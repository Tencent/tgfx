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

#include "GraphicsLoader.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
AutoGraphicsLoaderRestore::AutoGraphicsLoaderRestore(Context* context, GraphicsLoader* loader)
    : context(context) {
  if (context != nullptr && loader != nullptr) {
    oldLoader = context->proxyProvider()->graphicsLoader;
    context->proxyProvider()->graphicsLoader = loader;
    loader->onAttached();
  }
}

AutoGraphicsLoaderRestore::~AutoGraphicsLoaderRestore() {
  if (oldLoader != nullptr) {
    context->proxyProvider()->graphicsLoader->onDetached();
    context->proxyProvider()->graphicsLoader = oldLoader;
  }
}
}  // namespace tgfx