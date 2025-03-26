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

#include "tgfx/core/Document.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

Document::Document(std::shared_ptr<WriteStream> stream) : _stream(std::move(stream)) {
}

Document::~Document() {
  close();
}

Canvas* Document::beginPage(float pageWidth, float pageHeight, const Rect* contentRect) {
  if(pageWidth <= 0 || pageHeight <= 0||state == State::Closed) {
    return nullptr;
  }
  if(state == State::InPage) {
    endPage();
  }
  auto* canvas = onBeginPage(pageWidth, pageHeight);
  state = State::InPage;
  if (canvas && contentRect) {
    Rect rect = *contentRect;
    if (!rect.intersect({0, 0, pageWidth, pageWidth})) {
      return nullptr;
    }
    canvas->clipRect(rect);
    canvas->translate(rect.x(), rect.y());
  }
  return canvas;
}

void Document::endPage() {
  if(state == State::InPage) {
    onEndPage();
    state = State::BetweenPages;
  }
}

void Document::close() {
  while (true) {
    switch (state) {
      case State::BetweenPages:
        onClose();
        state = State::Closed;
        return;
      case State::InPage:
        endPage();
        break;
      case State::Closed:
        return;
    }
  }
}

void Document::abort() {
  if(state != State::Closed) {
    onAbort();
    state = State::Closed;
  }
}

}  // namespace tgfx