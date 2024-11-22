#include <iostream>
#include <string>
#include <thread>
#include <tgfx/core/Color.h>

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
