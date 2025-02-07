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

#include "tgfx/svg/ResourceLoader.h"

namespace tgfx {
class SystemResourceLoader final : public ResourceLoader {
 public:
  static std::shared_ptr<SystemResourceLoader> Make() {
    return std::shared_ptr<SystemResourceLoader>(new SystemResourceLoader);
  }

  std::shared_ptr<Data> loadData(const std::string& resourcePath,
                                 const std::string& resourceName) const override {
    if (resourceName.empty() && resourcePath.empty()) {
      return nullptr;
    }
    if (resourcePath.empty()) {
      return Data::MakeFromFile(resourceName);
    }
    if (resourceName.empty()) {
      return Data::MakeFromFile(resourcePath);
    }
    return Data::MakeFromFile(resourcePath + "/" + resourceName);
  };

  std::shared_ptr<Image> loadImage(const std::string& resourcePath,
                                   const std::string& resourceName) const override {
    if (resourceName.empty() && resourcePath.empty()) {
      return nullptr;
    }
    if (resourcePath.empty()) {
      return Image::MakeFromFile(resourceName);
    }
    if (resourceName.empty()) {
      return Image::MakeFromFile(resourcePath);
    }
    return Image::MakeFromFile(resourcePath + "/" + resourceName);
  }

 protected:
  SystemResourceLoader() = default;
};

}  // namespace tgfx