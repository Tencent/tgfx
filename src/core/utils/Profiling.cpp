/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
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

#ifdef TGFX_BUILD_INSPECTOR
#include "Profiling.h"
namespace inspector {
void SendAttributeData(const char* name, const tgfx::Rect& rect) {
  float value[4] = {rect.left, rect.right, rect.top, rect.bottom};
  Inspector::SendAttributeData(name, value, 4);
}

void SendAttributeData(const char* name, const tgfx::Matrix& matrix) {
  float value[6] = {1, 0, 0, 0, 1, 0};
  value[0] = matrix.getScaleX();
  value[1] = matrix.getSkewX();
  value[2] = matrix.getTranslateX();
  value[3] = matrix.getSkewY();
  value[4] = matrix.getScaleY();
  value[5] = matrix.getTranslateY();
  Inspector::SendAttributeData(name, value, 6);
}

void SendAttributeData(const char* name, const std::optional<tgfx::Matrix>& matrix) {
  auto value = tgfx::Matrix::MakeAll(1, 0, 0, 0, 1, 0);
  if (matrix.has_value()) {
    value = matrix.value();
  }
  SendAttributeData(name, value);
}

void SendAttributeData(const char* name, const tgfx::Color& color) {
  auto r = static_cast<uint8_t>(color.red * 255.f);
  auto g = static_cast<uint8_t>(color.green * 255.f);
  auto b = static_cast<uint8_t>(color.blue * 255.f);
  auto a = static_cast<uint8_t>(color.alpha * 255.f);
  auto value = static_cast<uint32_t>(r | g << 8 | b << 16 | a << 24);
  Inspector::SendAttributeData(name, value, MsgType::ValueDataColor);
}

void SendAttributeData(const char* name, const std::optional<tgfx::Color>& color) {
  auto value = tgfx::Color::FromRGBA(255, 255, 255, 255);
  if (color.has_value()) {
    value = color.value();
  }
  SendAttributeData(name, value);
}
}  // namespace inspector
#endif