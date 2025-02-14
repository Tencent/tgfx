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

#include "DataProvider.h"

namespace tgfx {
class DataWrapper : public DataProvider {
 public:
  explicit DataWrapper(std::shared_ptr<Data> data) : data(std::move(data)) {
  }

  std::shared_ptr<Data> getData() const override {
    return data;
  }

 private:
  std::shared_ptr<Data> data = nullptr;
};

std::unique_ptr<DataProvider> DataProvider::Wrap(std::shared_ptr<Data> data) {
  return std::make_unique<DataWrapper>(std::move(data));
}
}  // namespace tgfx