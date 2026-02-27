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

#include "PDFFormXObject.h"
#include <utility>
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/Stream.h"

namespace tgfx {

PDFIndirectReference MakePDFFormXObject(PDFDocumentImpl* document,
                                        std::shared_ptr<Data> contentData,
                                        std::unique_ptr<PDFArray> mediaBox,
                                        std::unique_ptr<PDFDictionary> resourceDictionary,
                                        const Matrix& inverseTransform, const char* colorSpace) {
  std::unique_ptr<PDFDictionary> dict = PDFDictionary::Make();
  dict->insertName("Type", "XObject");
  dict->insertName("Subtype", "Form");
  if (!inverseTransform.isIdentity()) {
    dict->insertObject("Matrix", PDFUtils::MatrixToArray(inverseTransform));
  }
  dict->insertObject("Resources", std::move(resourceDictionary));
  dict->insertObject("BBox", std::move(mediaBox));

  // Right now FormXObject is only used for saveLayer, which implies isolated blending. Do this
  // conditionally if that changes.
  auto group = PDFDictionary::Make("Group");
  group->insertName("S", "Transparency");
  if (colorSpace != nullptr) {
    group->insertName("CS", colorSpace);
  }
  group->insertBool("I", true);  // Isolated.
  dict->insertObject("Group", std::move(group));
  auto stream = Stream::MakeFromData(std::move(contentData));
  return PDFStreamOut(std::move(dict), std::move(stream), document);
}

}  // namespace tgfx
