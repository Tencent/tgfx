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
#include <string>
#include "core/FillStyle.h"
#include "core/utils/Log.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/MD5.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/UTF.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/pdf/PDFMetadata.h"

namespace tgfx {

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
  static std::unique_ptr<PDFArray> RectToArray(const Rect& rect);

  static std::unique_ptr<PDFArray> MatrixToArray(const Matrix& matrix);

  // Converts (value / 255.0) with three significant digits of accuracy.
  // Writes value as string into result.  Returns strlen() of result.
  static size_t ColorToDecimal(uint8_t value, char result[5]);

  static size_t ColorToDecimal(float value, char result[6]);

  static inline void AppendColorComponent(uint8_t value,
                                          const std::shared_ptr<WriteStream>& stream) {
    char buffer[5];
    size_t len = ColorToDecimal(value, buffer);
    stream->write(buffer, len);
  }

  static inline void AppendColorComponent(float value, const std::shared_ptr<WriteStream>& stream) {
    char buffer[7];
    size_t len = ColorToDecimal(value, buffer);
    stream->write(buffer, len);
  }

  static inline void WriteUInt16BE(const std::shared_ptr<WriteStream>& stream, uint16_t value) {
    char result[4] = {
        HexadecimalDigits::upper[value >> 12], HexadecimalDigits::upper[0xF & (value >> 8)],
        HexadecimalDigits::upper[0xF & (value >> 4)], HexadecimalDigits::upper[0xF & (value)]};
    stream->write(result, 4);
  }

  static inline void WriteUTF16beHex(const std::shared_ptr<WriteStream>& stream, Unichar utf32) {
    auto UTF16String = UTF::ToUTF16(utf32);
    WriteUInt16BE(stream, static_cast<uint16_t>(UTF16String[0]));
    if (UTF16String.size() == 2) {
      WriteUInt16BE(stream, static_cast<uint16_t>(UTF16String[1]));
    }
  }

  static void GetDateTime(DateTime* dataTime);

  static void AppendFloat(float value, const std::shared_ptr<WriteStream>& stream);

  static void AppendTransform(const Matrix& matrix, const std::shared_ptr<WriteStream>& stream);

  static void AppendRectangle(const Rect& rect, const std::shared_ptr<WriteStream>& content);

  static void ApplyGraphicState(int objectIndex, const std::shared_ptr<WriteStream>& content);

  static void ApplyPattern(int objectIndex, const std::shared_ptr<WriteStream>& content);

  static void EmitPath(const Path& path, bool doConsumeDegerates,
                       const std::shared_ptr<MemoryWriteStream>& content, float tolerance = 0.25f);

  static void PaintPath(PathFillType fillType, const std::shared_ptr<MemoryWriteStream>& content);

  static const char* BlendModeName(BlendMode mode);

  static bool InverseTransformBBox(const Matrix& matrix, Rect* boundBox);

  static void PopulateTilingPatternDict(PDFDictionary* pattern, const Rect& boundBox,
                                        std::unique_ptr<PDFDictionary> resources,
                                        const Matrix& matrix);
};

}  // namespace tgfx