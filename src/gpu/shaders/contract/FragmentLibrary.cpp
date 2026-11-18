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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/contract/FragmentLibrary.h"
#include <algorithm>

namespace tgfx {
namespace {

FragmentParam Param(const char* semantic, FragmentType type, uint16_t arraySize = 1,
                    bool programLevel = false) {
  FragmentParam param = {};
  param.semantic = semantic;
  param.type = type;
  param.arraySize = arraySize;
  param.programLevel = programLevel;
  return param;
}

FragmentArg Arg(FragmentArgKind kind, uint16_t index = 0) {
  FragmentArg arg = {};
  arg.kind = kind;
  arg.index = index;
  return arg;
}

const std::vector<FragmentDef>& Table() {
  static const std::vector<FragmentDef> table = {
      // ---- the pure pointwise color operators (ops/, moved verbatim) ----
      {ChainISA::ColorMatrix,
       FragmentKind::Function,
       "pmApplyColorMatrix",
       {Param("ColorMatrix.linear", FragmentType::Matrix4),
        Param("ColorMatrix.translate", FragmentType::Float4)},
       {Arg(FragmentArgKind::Input0), Arg(FragmentArgKind::Param, 0),
        Arg(FragmentArgKind::Param, 1)},
       {}},
      {ChainISA::Luma,
       FragmentKind::Function,
       "pmApplyLuma",
       {Param("Luma.coefficients", FragmentType::Float3)},
       {Arg(FragmentArgKind::Input0), Arg(FragmentArgKind::Param, 0)},
       {}},
      {ChainISA::AlphaThreshold,
       FragmentKind::Function,
       "pmApplyAlphaThreshold",
       {Param("AlphaThreshold.threshold", FragmentType::Float)},
       {Arg(FragmentArgKind::Input0), Arg(FragmentArgKind::Param, 0)},
       {}},
      {ChainISA::ConstColor,
       FragmentKind::Function,
       "pmApplyConstColor",
       {Param("ConstColor.color", FragmentType::Float4)},
       {Arg(FragmentArgKind::Param, 0), Arg(FragmentArgKind::Input0),
        Arg(FragmentArgKind::Literal)},
       {}},
      {ChainISA::ColorSpaceXform,
       FragmentKind::EntryCoupled,
       "chainApplyColorSpaceXform",
       {},
       {Arg(FragmentArgKind::Input0)},
       {}},
      {ChainISA::Blend,
       FragmentKind::Function,
       "xpBlendColors",
       {},
       {Arg(FragmentArgKind::Input0), Arg(FragmentArgKind::Input1), Arg(FragmentArgKind::Literal)},
       {"xp/xp_blend_colors.inc"}},
      // ---- the texture operators (ops/, pure: the entry supplies the fetched sample) ----
      {ChainISA::Texture,
       FragmentKind::Function,
       "tgfxOpTexture",
       {},
       {Arg(FragmentArgKind::EntryValue), Arg(FragmentArgKind::EntryValue, 1),
        Arg(FragmentArgKind::EntryValue, 2), Arg(FragmentArgKind::Literal)},
       {"ops/Texture.glsl", "source/texture_read_swizzle.inc"}},
      {ChainISA::TexModulate,
       FragmentKind::Function,
       "tgfxOpTexModulate",
       {},
       {Arg(FragmentArgKind::EntryValue), Arg(FragmentArgKind::Input0),
        Arg(FragmentArgKind::Literal)},
       {"ops/TexModulate.glsl", "source/texture_read_swizzle.inc"}},
      // ---- the analytic coverage operators (ops/, pure: the entry supplies the coordinate) ----
      {ChainISA::AARectCoverage,
       FragmentKind::Function,
       "tgfxOpAARectCoverage",
       {Param("AARectCoverage.rect", FragmentType::Float4)},
       {Arg(FragmentArgKind::Input0), Arg(FragmentArgKind::Param, 0),
        Arg(FragmentArgKind::EntryValue, 3)},
       {"ops/AARectCoverage.glsl"}},
      {ChainISA::LocalRectCoverage,
       FragmentKind::Function,
       "tgfxOpLocalRectCoverage",
       {Param("LocalRectCoverage.rect", FragmentType::Float4),
        Param("LocalRectCoverage.deviceToLocal", FragmentType::Matrix3)},
       {Arg(FragmentArgKind::Input0), Arg(FragmentArgKind::Param, 0),
        Arg(FragmentArgKind::Param, 1), Arg(FragmentArgKind::EntryValue, 3)},
       {"ops/LocalRectCoverage.glsl", "coverage/analytic_clip_math.inc"}},
      {ChainISA::RRectCoverage,
       FragmentKind::Function,
       "tgfxOpRRectCoverage",
       {Param("RRectCoverage.rect", FragmentType::Float4),
        Param("RRectCoverage.radiiX", FragmentType::Float4),
        Param("RRectCoverage.radiiY", FragmentType::Float4),
        Param("RRectCoverage.antiAlias", FragmentType::Float),
        Param("RRectCoverage.deviceToLocal", FragmentType::Matrix3)},
       {Arg(FragmentArgKind::Input0), Arg(FragmentArgKind::Param, 0),
        Arg(FragmentArgKind::Param, 1), Arg(FragmentArgKind::Param, 2),
        Arg(FragmentArgKind::Param, 3), Arg(FragmentArgKind::Param, 4),
        Arg(FragmentArgKind::EntryValue, 3)},
       {"ops/RRectCoverage.glsl", "coverage/analytic_clip_math.inc"}},
      // ---- the inline expressions (no file, the assembler spells them) ----
      {ChainISA::InputOpaque, FragmentKind::Expression, "", {}, {Arg(FragmentArgKind::Input0)}, {}},
      {ChainISA::MulAlpha,
       FragmentKind::Expression,
       "",
       {},
       {Arg(FragmentArgKind::Input0), Arg(FragmentArgKind::Input1)},
       {}},
      // ---- the entry-coupled resource operators (shared evaluators, pure split later) ----
      {ChainISA::Gradient,
       FragmentKind::EntryCoupled,
       "evalGradientSlot",
       {Param("Gradient.block", FragmentType::Float4, 18, /*programLevel=*/true)},
       {Arg(FragmentArgKind::Input0)},
       {"pipeline/chain/pointwise_gradient.inc"}},
      {ChainISA::PerlinNoise,
       FragmentKind::EntryCoupled,
       "evalNoiseSlot",
       {Param("PerlinNoise.frequencyStitch", FragmentType::Float4)},
       {Arg(FragmentArgKind::EntryValue, 3)},
       {"pipeline/chain/pointwise_noise.inc"}},
      {ChainISA::None, FragmentKind::Expression, "", {}, {}, {}},
  };
  return table;
}

}  // namespace

const FragmentDef* FindFragment(int op) {
  const auto& table = Table();
  const auto found = std::find_if(table.begin(), table.end(),
                                  [op](const FragmentDef& fragment) { return fragment.op == op; });
  return found == table.end() ? nullptr : &*found;
}

size_t FragmentCount() {
  return Table().size();
}

}  // namespace tgfx
