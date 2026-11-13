/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/write/modules/YuvTail.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/aot/write/modules/ClipWrite.h"
#include "gpu/shaders/modules/YUVChainTail.h"

namespace tgfx {

void WriteYuvTailModule(WriteContext& context) {
  // The analytic clip fields live in this module (bare names, the legacy spelling); the tail's slot
  // records come from the compiled program through the same binder the legacy kernel used.
  WriteClipParams<YUVChainTail>(context.info, context.slot);
  if (const auto* program = AOTChainBuilder::ResolveYUVProgram(&context.info)) {
    BindYUVProgram(*program, context.slot);
  }
}

ModuleWriter YuvTailWriterOf(const Module& module) {
  if (&module == &YUVChainTail) {
    return WriteYuvTailModule;
  }
  return nullptr;
}

}  // namespace tgfx
