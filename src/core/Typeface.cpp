/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Typeface.h"
#include <vector>
#include "core/TypefaceMetrics.h"
#include "core/utils/UniqueID.h"
#include "tgfx/core/UTF.h"

namespace tgfx {
class EmptyTypeface : public Typeface {
 public:
  uint32_t uniqueID() const override {
    return _uniqueID;
  }

  std::string fontFamily() const override {
    return "";
  }

  std::string fontStyle() const override {
    return "";
  }

  size_t glyphsCount() const override {
    return 0;
  }

  int unitsPerEm() const override {
    return 0;
  }

  bool hasColor() const override {
    return false;
  }

  bool hasOutlines() const override {
    return false;
  }

  GlyphID getGlyphID(Unichar) const override {
    return 0;
  }

  std::shared_ptr<Data> getBytes() const override {
    return nullptr;
  }

  std::shared_ptr<Data> copyTableData(FontTableTag) const override {
    return nullptr;
  }

 protected:
  std::vector<Unichar> getGlyphToUnicodeMap() const override {
    return {};
  }

  std::shared_ptr<Data> openData() const override {
    return nullptr;
  }

  std::unique_ptr<TypefaceMetrics> onGetMetrics() const override {
    return nullptr;
  };

 private:
  uint32_t _uniqueID = UniqueID::Next();
};

std::shared_ptr<Typeface> Typeface::MakeEmpty() {
  static auto emptyTypeface = std::make_shared<EmptyTypeface>();
  return emptyTypeface;
}

GlyphID Typeface::getGlyphID(const std::string& name) const {
  if (name.empty()) {
    return 0;
  }
  auto count = UTF::CountUTF8(name.c_str(), name.size());
  if (count <= 0) {
    return 0;
  }
  const char* start = name.data();
  auto unichar = UTF::NextUTF8(&start, start + name.size());
  return getGlyphID(unichar);
}

std::vector<Unichar> Typeface::getGlyphToUnicodeMap() const {
  return {};
};

size_t Typeface::getTableSize(FontTableTag tag) const {
  auto data = copyTableData(tag);
  if (data) {
    return data->size();
  }
  return 0;
}

std::unique_ptr<TypefaceMetrics> Typeface::getMetrics() const {
  std::unique_ptr<TypefaceMetrics> result = onGetMetrics();
  if (result && result->postScriptName.empty()) {
    result->postScriptName = this->fontFamily();
  }
  if (result && (result->type == TypefaceMetrics::FontType::TrueType ||
                 result->type == TypefaceMetrics::FontType::CFF)) {
    // SkOTTableOS2::Version::V2::Type::Field fsType;
    // constexpr SkFontTableTag os2Tag = SkTEndian_SwapBE32(SkOTTableOS2::TAG);
    // constexpr size_t fsTypeOffset = offsetof(SkOTTableOS2::Version::V2, fsType);
    // if (this->getTableData(os2Tag, fsTypeOffset, sizeof(fsType), &fsType) == sizeof(fsType)) {
    //   if (fsType.Bitmap || (fsType.Restricted && !(fsType.PreviewPrint || fsType.Editable))) {
    //     result->fFlags |= SkAdvancedTypefaceMetrics::kNotEmbeddable_FontFlag;
    //   }
    //   if (fsType.NoSubsetting) {
    //     result->fFlags |= SkAdvancedTypefaceMetrics::kNotSubsettable_FontFlag;
    //   }
    // }
  }
  return result;
}

}  // namespace tgfx