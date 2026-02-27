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

#include "pdf/PDFTypes.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

enum class PDFResourceType {
  ExtGState,
  Pattern,
  XObject,
  Font,
};

std::unique_ptr<PDFDictionary> MakePDFResourceDictionary(
    const std::vector<PDFIndirectReference>& graphicStateResources,
    const std::vector<PDFIndirectReference>& shaderResources,
    const std::vector<PDFIndirectReference>& xObjectResources,
    const std::vector<PDFIndirectReference>& fontResources);

void PDFWriteResourceName(const std::shared_ptr<WriteStream>& stream, PDFResourceType type,
                          int key);

}  // namespace tgfx
