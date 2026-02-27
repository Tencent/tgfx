/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <array>
#include "pdf/PDFTypes.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/pdf/PDFMetadata.h"

namespace tgfx {

class HexadecimalDigits {
 public:
  static inline const std::array<char, 16> upper = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

  static inline const std::array<char, 16> lower = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
};

struct UUID {
  std::array<uint8_t, 16> data = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
};

static inline bool operator==(const UUID& u, const UUID& v) {
  return 0 == memcmp(u.data.data(), v.data.data(), u.data.size());
}

static inline bool operator!=(const UUID& u, const UUID& v) {
  return !(u == v);
}

class PDFUtils {
 public:
  static void GetDateTime(DateTime* dataTime);

  static std::unique_ptr<PDFArray> RectToArray(const Rect& rect);

  static std::unique_ptr<PDFArray> MatrixToArray(const Matrix& matrix);

  static size_t ColorToDecimal(uint8_t value, char result[5]);

  static size_t ColorToDecimal(float value, char result[6]);

  static void AppendColorComponent(uint8_t value, const std::shared_ptr<WriteStream>& stream);

  static void AppendColorComponent(float value, const std::shared_ptr<WriteStream>& stream);

  static void WriteUInt8(const std::shared_ptr<WriteStream>& stream, uint8_t value);

  static void WriteUInt16BE(const std::shared_ptr<WriteStream>& stream, uint16_t value);

  static void WriteUTF16beHex(const std::shared_ptr<WriteStream>& stream, Unichar utf32);

  static void AppendFloat(float value, const std::shared_ptr<WriteStream>& stream);

  static void AppendTransform(const Matrix& matrix, const std::shared_ptr<WriteStream>& stream);

  static void AppendRectangle(const Rect& rect, const std::shared_ptr<WriteStream>& content);

  static void ApplyGraphicState(int objectIndex, const std::shared_ptr<WriteStream>& content);

  static void ApplyPattern(int objectIndex, const std::shared_ptr<WriteStream>& content);

  static void EmitPath(const Path& path, bool doConsumeDegerates,
                       const std::shared_ptr<MemoryWriteStream>& content);

  static void PaintPath(PathFillType fillType, const std::shared_ptr<MemoryWriteStream>& content);

  static const char* BlendModeName(BlendMode mode);

  static bool InverseTransformBBox(const Matrix& matrix, Rect* boundBox);

  static void PopulateTilingPatternDict(PDFDictionary* pattern, const Rect& boundBox,
                                        std::unique_ptr<PDFDictionary> resources,
                                        const Matrix& matrix);
};

}  // namespace tgfx
