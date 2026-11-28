/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/UniformLayout.h"

namespace tgfx {
namespace {

struct Row {
  SLType type = SLType::Void;
  Std140Layout layout = {};
};

// Indexed by the format's pinned value (see the static_assert in Uniform.h): one row per
// enumerator, in enumeration order. The table is the only place the numbers live; adding a format
// means adding a row here, and the assertion below makes a missing row a build error.
constexpr Row kRows[] = {
    {SLType::Float, {4, 4, 16}},           // Float
    {SLType::Float2, {8, 8, 16}},          // Float2
    {SLType::Float3, {12, 16, 16}},        // Float3
    {SLType::Float4, {16, 16, 16}},        // Float4
    {SLType::Float2x2, {32, 16, 32}},      // Float2x2
    {SLType::Float3x3, {48, 16, 48}},      // Float3x3
    {SLType::Float4x4, {64, 16, 64}},      // Float4x4
    {SLType::Int, {4, 4, 16}},             // Int
    {SLType::Int2, {8, 8, 16}},            // Int2
    {SLType::Int3, {12, 16, 16}},          // Int3
    {SLType::Int4, {16, 16, 16}},          // Int4
    {SLType::Texture2DSampler, {}},        // Texture2DSampler: a binding, not a block member
    {SLType::TextureExternalSampler, {}},  // TextureExternalSampler
    {SLType::Texture2DRectSampler, {}},    // Texture2DRectSampler
};

constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

static_assert(kRowCount == static_cast<size_t>(UniformFormat::Texture2DRectSampler) + 1,
              "kRows must carry exactly one row per UniformFormat, in enumeration order");

}  // namespace

Std140Layout Std140LayoutOf(UniformFormat format) {
  const auto index = static_cast<size_t>(format);
  return index < kRowCount ? kRows[index].layout : Std140Layout{};
}

Std140Layout Std140LayoutOf(SLType type) {
  for (const auto& row : kRows) {
    if (row.type == type) {
      return row.layout;
    }
  }
  return {};
}

SLType SLTypeOf(UniformFormat format) {
  const auto index = static_cast<size_t>(format);
  return index < kRowCount ? kRows[index].type : SLType::Void;
}

uint32_t Std140AlignUp(uint32_t offset, uint32_t alignment) {
  if (alignment == 0) {
    return offset;
  }
  return (offset + alignment - 1) / alignment * alignment;
}

}  // namespace tgfx
