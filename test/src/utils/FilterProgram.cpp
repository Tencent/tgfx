/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "FilterProgram.h"
#include "utils/TestUtils.h"

namespace tgfx {
std::unique_ptr<FilterProgram> FilterProgram::Make(Context* context, const std::string& vertex,
                                                   const std::string& fragment) {
  auto gl = GLFunctions::Get(context);
  auto program = CreateGLProgram(context, vertex, fragment);
  if (program == 0) {
    return nullptr;
  }
  auto filterProgram = std::unique_ptr<FilterProgram>(new FilterProgram(context));
  filterProgram->program = program;
  if (gl->bindVertexArray != nullptr) {
    gl->genVertexArrays(1, &filterProgram->vertexArray);
  }
  gl->genBuffers(1, &filterProgram->vertexBuffer);
  return filterProgram;
}

void FilterProgram::onReleaseGPU() {
  auto gl = GLFunctions::Get(getContext());
  if (program > 0) {
    gl->deleteProgram(program);
    program = 0;
  }
  if (vertexArray > 0) {
    gl->deleteVertexArrays(1, &vertexArray);
    vertexArray = 0;
  }
  if (vertexBuffer > 0) {
    gl->deleteBuffers(1, &vertexBuffer);
    vertexBuffer = 0;
  }
}
}  // namespace tgfx
