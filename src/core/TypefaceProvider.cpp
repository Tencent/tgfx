/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/TypefaceProvider.h"

namespace tgfx {
TypefaceProviderManager* TypefaceProviderManager::GetInstance() {
  static auto& manager = *new TypefaceProviderManager();
  return &manager;
}

void TypefaceProviderManager::registerProvider(std::shared_ptr<TypefaceProvider> provider) {
  std::lock_guard<std::mutex> autoLock(locker);
  _provider = provider;
}
}  // namespace tgfx