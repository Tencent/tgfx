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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/pdf/PDFMetadata.h"

namespace tgfx {

/**
 * PDFDocument is a class used for exporting PDF documents.
 */
class PDFDocument {
 public:
  /**
   * Creates a PDF document.
   * @param stream The output stream where the PDF file will be written.
   * @param context The GPU context used for processing images.
   * @param metadata Metadata describing the PDF file.
   * @return A Document object that provides interfaces for import operations.
   */
  static std::shared_ptr<PDFDocument> Make(std::shared_ptr<WriteStream> stream, Context* context,
                                           PDFMetadata metadata);

  /**
   * Destroy the PDFDocument object
   */
  virtual ~PDFDocument() = default;

  /**
   * Creates a new page with the given width and height. If contentRect is provided, content will be
   * clipped to this area. Returns nullptr if the document has been closed.
   */
  virtual Canvas* beginPage(float pageWidth, float pageHeight,
                            const Rect* contentRect = nullptr) = 0;

  /**
   * Ends the current page.
   */
  virtual void endPage() = 0;

  /**
   * Ends the current page and closes the document.
   */
  virtual void close() = 0;

  /**
   * Aborts the document and discards all writes.
   */
  virtual void abort() = 0;
};

}  // namespace tgfx
