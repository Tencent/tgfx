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
#include "gpu/ResourceKey.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathProvider.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shape.h"

#pragma once

namespace tgfx {
class ProviderShape final : public Shape {
 public:
  explicit ProviderShape(std::shared_ptr<PathProvider> pathProvider)
      : provider(std::move(pathProvider)) {
  }

  Rect getBounds(float resolutionScale = 1.0f) const override;

  Path getPath(float resolutionScale = 1.0f) const override;

 protected:
  Type type() const override {
    return Type::Provider;
  }

  UniqueKey getUniqueKey() const override {
    return uniqueKey.get();
  }

 private:
  std::shared_ptr<PathProvider> provider = nullptr;
  LazyUniqueKey uniqueKey = {};
};
}  // namespace tgfx
