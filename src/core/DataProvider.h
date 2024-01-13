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

#pragma once

#include "tgfx/core/Data.h"

namespace tgfx {
/**
 * DataProvider delays the data generation until it is needed.
 */
class DataProvider {
 public:
  static std::shared_ptr<DataProvider> Wrap(std::shared_ptr<Data> data);

  virtual ~DataProvider() = default;

  /**
   * Generates the data. DataProvider does not cache the data, each call to getData() will
   * generate a new data.
   */
  virtual std::shared_ptr<Data> getData() const = 0;
};
}  // namespace tgfx
