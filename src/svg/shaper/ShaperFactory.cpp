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

#include "tgfx/svg/shaper/ShaperFactory.h"
#include <memory>
#include "svg/shaper/ShaperPrimitive.h"
#include "tgfx/svg/shaper/Shaper.h"

namespace tgfx {

namespace {
class PrimitiveFactoryImpl final : public shapers::Factory {
  std::unique_ptr<Shaper> makeShaper() override {
    return std::make_unique<ShaperPrimitive>();
  }

  std::unique_ptr<BiDiRunIterator> makeBidiRunIterator(const char*, size_t, uint8_t) override {
    return std::make_unique<TrivialBiDiRunIterator>(0, 0);
  }

  std::unique_ptr<ScriptRunIterator> makeScriptRunIterator(const char*, size_t,
                                                           FourByteTag) override {
    return std::make_unique<TrivialScriptRunIterator>(0, 0);
  }
};
}  // namespace

namespace shapers {
std::shared_ptr<Factory> PrimitiveFactory() {
  return std::make_shared<PrimitiveFactoryImpl>();
}
}  // namespace shapers

}  // namespace tgfx