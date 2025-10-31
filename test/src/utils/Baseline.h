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

#include "core/PixelBuffer.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Surface.h"

namespace tgfx {
class Baseline {
 public:
  static bool Compare(std::shared_ptr<PixelBuffer> pixelBuffer, const std::string& key);

  static bool Compare(std::shared_ptr<Surface> surface, const std::string& key);

  static bool Compare(const Bitmap& bitmap, const std::string& key);

  static bool Compare(const Pixmap& pixmap, const std::string& key);

  static bool Compare(std::shared_ptr<Data> data, const std::string& key);

 private:
  static void SetUp();

  static void TearDown();

  friend class TestEnvironment;
};
}  // namespace tgfx
