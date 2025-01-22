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

#include <memory>
#include "tgfx/svg/shaper/Shaper.h"

namespace tgfx {
namespace shapers {

class Factory {
 public:
  virtual ~Factory() = default;

  virtual std::unique_ptr<Shaper> makeShaper() = 0;

  virtual std::unique_ptr<BiDiRunIterator> makeBidiRunIterator(const char* utf8, size_t utf8Bytes,
                                                               uint8_t bidiLevel) = 0;

  virtual std::unique_ptr<ScriptRunIterator> makeScriptRunIterator(const char* utf8,
                                                                   size_t utf8Bytes,
                                                                   FourByteTag script) = 0;
};

std::shared_ptr<Factory> PrimitiveFactory();

}  // namespace shapers
}  // namespace tgfx