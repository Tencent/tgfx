/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "QGLProcGetter.h"

namespace tgfx {
void* QGLProcGetter::getProcAddress(const char* name) const {
  return reinterpret_cast<void*>(glContext->getProcAddress(name));
}

std::unique_ptr<GLProcGetter> GLProcGetter::Make() {
  auto context = QOpenGLContext::currentContext();
  if (context == nullptr) {
    return nullptr;
  }
  return std::make_unique<QGLProcGetter>(context);
}
}  // namespace tgfx
