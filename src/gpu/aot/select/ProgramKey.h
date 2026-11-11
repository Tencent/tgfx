/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties of any kind, either express or
//  implied. see the license for the specific language governing permissions and limitations under
//  the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include "tgfx/core/BytesKey.h"

namespace tgfx {

class Context;
class ProgramInfo;
struct SpecValues;

/**
 * The one program-cache key. A precompiled program is named by everything a program is: the
 * variant that selects its blob pair, the specialization values folded into the shader, the
 * geometry processor's vertex layout, the xfer processor, the blend-formula bit and the pipeline
 * state. The draw's processor list is deliberately absent — processors only supply uniform values,
 * which the writer uploads per draw and which never change which program serves the draw.
 * @param context    The context whose backend the processors' keys are scoped to.
 * @param info       The draw's matched processors and pipeline state.
 * @param variantId  The matched variant: family index << 20 | ordinal.
 * @param spec       The specialization values; an empty set addresses the generic form.
 */
BytesKey MakeProgramKey(Context* context, const ProgramInfo& info, uint32_t variantId,
                        const SpecValues& spec);

}  // namespace tgfx
