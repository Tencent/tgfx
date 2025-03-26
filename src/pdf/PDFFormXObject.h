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
#include "pdf/PDFTypes.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

/** A form XObject is a self contained description of a graphics
    object.  A form XObject is a page object with slightly different
    syntax, that can be drawn into a page content stream, just like a
    bitmap XObject can be drawn into a page content stream.
*/
PDFIndirectReference MakePDDFormXObject(PDFDocument* document, std::shared_ptr<Data> contentData,
                                        std::unique_ptr<PDFArray> mediaBox,
                                        std::unique_ptr<PDFDictionary> resourceDictionary,
                                        const Matrix& inverseTransform, const char* colorSpace);

}  // namespace tgfx