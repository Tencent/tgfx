/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <memory>
#include "tgfx/core/Data.h"

namespace tgfx {
struct FTFontData {
  FTFontData(std::string path, int ttcIndex) : path(std::move(path)), ttcIndex(ttcIndex) {
  }

  FTFontData(std::shared_ptr<Data> data, int ttcIndex) : data(std::move(data)), ttcIndex(ttcIndex) {
  }

  std::string path;
  std::shared_ptr<Data> data;
  int ttcIndex = -1;
};
}  // namespace tgfx
