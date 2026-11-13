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

#include "gpu/aot/write/modules/Frame.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/aot/write/ModuleWriter.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/shaders/modules/Frame.h"

namespace tgfx {

void WriteFrameModule(WriteContext& context) {
  constexpr uint8_t RTAdjust = UniformElementIndexOf(Frame, "RTAdjust");
  context.slot->set(RTAdjust, GetRTAdjustArray(context.info.getRenderTarget()));
}

}  // namespace tgfx
