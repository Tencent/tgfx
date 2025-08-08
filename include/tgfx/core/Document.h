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

namespace tgfx {

constexpr float ScalarDefaultRasterDPI = 72.0f;

/**
 * Document is a base abstract class for export documents.
 */
class Document {
 public:
  virtual ~Document();

  /**
   * Creates a new page with the given width and height. If contentRect is provided, content will be
   * clipped to this area. Returns nullptr if the document has been closed.
   */
  Canvas* beginPage(float pageWidth, float pageHeight, const Rect* contentRect = nullptr);

  /**
   * Ends the current page.
   */
  void endPage();

  /**
   * Ends the current page and closes the document.
   */
  void close();

  /**
   * Aborts the document and discards all writes.
   */
  void abort();

 protected:
  explicit Document(std::shared_ptr<WriteStream> stream);

  virtual Canvas* onBeginPage(float pageWidth, float pageHeight) = 0;

  virtual void onEndPage() = 0;

  virtual void onClose() = 0;

  virtual void onAbort() = 0;

  std::shared_ptr<WriteStream> stream() const {
    return _stream;
  }

 private:
  enum class State {
    BetweenPages,
    InPage,
    Closed,
  };

  std::shared_ptr<WriteStream> _stream = nullptr;
  State state = State::BetweenPages;
};

}  // namespace tgfx