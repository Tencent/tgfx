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

#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/pdf/PDFMetadata.h"

namespace tgfx {
class PDFMetadataUtils {
 public:
  static std::unique_ptr<PDFObject> MakeDocumentInformationDict(const PDFMetadata& metadata);

  static UUID CreateUUID(const PDFMetadata& metadata);

  static std::unique_ptr<PDFObject> MakePDFId(const UUID& doc, const UUID& instance);

  static PDFIndirectReference MakeXMPObject(const PDFMetadata& metadata, const UUID& doc,
                                            const UUID& instance, PDFDocumentImpl*);
};

}  // namespace tgfx
