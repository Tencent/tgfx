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

#include "tgfx/core/LoadResourceProvider.h"
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace tgfx {

class FileResourceProvider final : public LoadResourceProvider {
 public:
  explicit FileResourceProvider(std::string basePath) : basePath(std::move(basePath)) {
  }

  std::shared_ptr<Data> load(const std::string& resourcePath,
                             const std::string& resourceName) const override {
    return Data::MakeFromFile(basePath + "/" + resourcePath + "/" + resourceName);
  };

  std::shared_ptr<Image> loadImage(const std::string& resourcePath,
                                   const std::string& resourceName) const override {
    return Image::MakeFromFile(basePath + "/" + resourcePath + "/" + resourceName);
  }

 private:
  const std::string basePath;
};

std::shared_ptr<LoadResourceProvider> LoadResourceProvider::MakeEmpty() {
  return std::shared_ptr<LoadResourceProvider>(new LoadResourceProvider);
}

std::shared_ptr<LoadResourceProvider> LoadResourceProvider::MakeFileProvider(
    const std::string& basePath) {
  return std::make_shared<FileResourceProvider>(basePath);
}

}  // namespace tgfx