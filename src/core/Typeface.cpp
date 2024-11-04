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
}  // namespace tgfx