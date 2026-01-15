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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "core/GlyphRunList.h"
#include "core/RunRecord.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {

GlyphRunList::Iterator GlyphRunList::begin() const {
  return Iterator(blob->firstRun(), blob->runCount);
}

bool GlyphRunList::empty() const {
  return blob->runCount == 0;
}

GlyphRun GlyphRunList::Iterator::operator*() const {
  return GlyphRun::From(current);
}

GlyphRunList::Iterator& GlyphRunList::Iterator::operator++() {
  current = current->next();
  --remaining;
  return *this;
}

}  // namespace tgfx
