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

#include <memory>

namespace tgfx {

class PDFArray;
class PDFGlyphUse;
class PDFStrikeSpec;

/* PDF 32000-1:2008, page 270: "The array's elements have a variable
   format that can specify individual widths for consecutive CIDs or
   one width for a range of CIDs". */
std::unique_ptr<PDFArray> PDFMakeCIDGlyphWidthsArray(const PDFStrikeSpec& strikeSpec,
                                                     const PDFGlyphUse& subset,
                                                     int32_t* defaultAdvance);

}  // namespace tgfx
