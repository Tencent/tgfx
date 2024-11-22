/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <emscripten/bind.h>
#include "FigmaRenderer.h"

using namespace emscripten;

EMSCRIPTEN_BINDINGS(figma_module) {
  class_<FigmaRenderer>("FigmaRenderer")
      .constructor<>()
      .function("initialize", &FigmaRenderer::initialize)
      .function("invalisize", &FigmaRenderer::invalisize)
      .function("updateShape", &FigmaRenderer::updateShape)
      .function("handMessage", &FigmaRenderer::handMessage);
}

int main(int, const char*[]) {
  return 0;
}