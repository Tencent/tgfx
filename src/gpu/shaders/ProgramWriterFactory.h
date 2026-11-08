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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <string>

namespace tgfx {

class ProgramWriter;

/**
 * The writer one family's programs own: the chain family's fused writer, the YUV family's tail
 * writer, or the generic precompiled walk. It lives with the families it names — the program creator
 * and the specialization capture both ask here, so the one place a family's writer is chosen is the
 * declaration layer rather than the artifact store.
 */
std::unique_ptr<ProgramWriter> MakeProgramWriter(const std::string& shaderName);

}  // namespace tgfx
