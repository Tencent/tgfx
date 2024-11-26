/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <emscripten/bind.h>
#include "tgfx/core/Data.h"

using namespace tgfx;
using namespace emscripten;

class DataLoaderImpl : public DataLoader {
 public:
  std::shared_ptr<Data> makeFromFile(const std::string& filePath) override {
    val global = val::global("window");
    val makeFromFileFunc = global["makeFromFile"];
    val emscriptenData = makeFromFileFunc(filePath).await();
    if (emscriptenData.isUndefined()) {
      return nullptr;
    }
    unsigned int length = emscriptenData["length"].as<unsigned int>();
    if (length == 0) {
      return nullptr;
    }
    auto buffer = new (std::nothrow) uint8_t[length];
    if (buffer) {
      auto memory = val::module_property("HEAPU8")["buffer"];
      auto memoryView =
          emscriptenData["constructor"].new_(memory, reinterpret_cast<uintptr_t>(buffer), length);
      memoryView.call<void>("set", emscriptenData);
      return tgfx::Data::MakeAdopted(buffer, length, tgfx::Data::DeleteProc);
    }
    return nullptr;
  }
};