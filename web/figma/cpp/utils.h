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


#include <iostream>
#include <string>
#include <thread>
#include <tgfx/core/Color.h>
#include <emscripten/console.h>

#include <fstream>

inline void printFileInfo(const std::string& filePath) {
  // 打开文件
  std::ifstream file(filePath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Error: Failed to open file " << filePath << std::endl;
    return;
  }
  file.close();
}


inline std::shared_ptr<tgfx::Data> getDataFromEmscripten(const emscripten::val& native_font) {
  if (native_font.isUndefined()) {
    return nullptr;
  }
  unsigned int length = native_font["length"].as<unsigned int>();
  if (length == 0) {
    return nullptr;
  }
  auto buffer = new (std::nothrow) uint8_t[length];
  if (buffer) {
    auto memory = emscripten::val::module_property("HEAPU8")["buffer"];
    auto memoryView =
        native_font["constructor"].new_(memory, reinterpret_cast<uintptr_t>(buffer), length);
    memoryView.call<void>("set", native_font);
    return tgfx::Data::MakeAdopted(buffer, length, tgfx::Data::DeleteProc);
  }
  return nullptr;
}


inline void printLayerBounds(tgfx::Layer* layer, const int indent) {
  if (layer == nullptr) return;
  const std::string indentation(static_cast<std::string::size_type>(indent * 2), ' ');  // 每层缩进2个空格
  const tgfx::Rect bounds = layer->getBounds();
  std::cout << indentation << "Layer bounds: " << bounds.x() << ", " << bounds.y() << ", " << bounds.width() << ", " << bounds.height() << std::endl;
  for (auto child : layer->children()) {
    printLayerBounds(child.get(), indent + 1);
  }
}

tgfx::Color inline MakeColorFromHexString(const std::string& hex) {
    if (hex.empty() || hex[0] != '#' || (hex.length() != 7 && hex.length() != 9)) {
        std::cout << "Invalid hex color string" << std::endl;
        return tgfx::Color::Black();
    }

    uint8_t r = static_cast<uint8_t>(std::stoi(hex.substr(1, 2), nullptr, 16));
    uint8_t g = static_cast<uint8_t>(std::stoi(hex.substr(3, 2), nullptr, 16));
    uint8_t b = static_cast<uint8_t>(std::stoi(hex.substr(5, 2), nullptr, 16));
    uint8_t a = 255; // Default alpha to 255 (opaque)

    if (hex.length() == 9) {
        a = static_cast<uint8_t>(std::stoi(hex.substr(7, 2), nullptr, 16));
    }

    return tgfx::Color::FromRGBA(r, g, b, a);
}
