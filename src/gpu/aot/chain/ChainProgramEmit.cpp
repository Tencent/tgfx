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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/ChainProgramEmit.h"

// The scanned function catalog (shader_build_tool --catalog). Builds that do not generate it fall
// back to the pack entries.
#if __has_include("ChainFunctionCatalog.h")
#include "ChainFunctionCatalog.h"
#define TGFX_HAS_CHAIN_CATALOG 1
#endif
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/chain/ChainProgramPlan.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/FragmentLibrary.h"

namespace tgfx {

#if defined(TGFX_HAS_CHAIN_CATALOG)
// The catalog's requires list is inferred from the function bodies, so emitting a function after its
// dependencies gives a text whose callees are always spelled before the call. `emitted` is per
// program: two programs never share a prefix.
void AppendCatalogFunction(const char* name, std::set<std::string>* emitted, std::string* out) {
  if (name == nullptr || name[0] == '\0' || !emitted->insert(name).second) {
    return;
  }
  for (const auto& entry : kChainFunctions) {
    if (entry.name == nullptr || std::strcmp(entry.name, name) != 0) {
      continue;
    }
    const std::string requires = entry.requires != nullptr ? entry.requires : "";
    size_t start = 0;
    while (start <= requires.size()) {
      const size_t comma = requires.find(',', start);
      const std::string dep =
          requires.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      if (!dep.empty()) {
        AppendCatalogFunction(dep.c_str(), emitted, out);
      }
      if (comma == std::string::npos) {
        break;
      }
      start = comma + 1;
    }
    out->append(entry.source);
    out->push_back('\n');
    return;
  }
}
#endif

namespace {

const char* GlslTypeOf(FragmentType type) {
  switch (type) {
    case FragmentType::Float:
      return "float";
    case FragmentType::Float2:
      return "vec2";
    case FragmentType::Float3:
      return "vec3";
    case FragmentType::Float4:
      return "vec4";
    case FragmentType::Int:
      return "int";
    case FragmentType::Int4:
      return "ivec4";
    case FragmentType::Matrix3:
      return "mat3";
    case FragmentType::Matrix4:
      return "mat4";
  }
  return "vec4";
}

}  // namespace

namespace {

// The blend-mode function the catalog declares for each BlendMode value (the split table).
const char* BlendModeFunctionName(int mode) {
  static const char* kNames[] = {
      "Clear",       "Src",       "Dst",        "SrcOver",   "DstOver",    "SrcIn",
      "DstIn",       "SrcOut",    "DstOut",     "SrcATop",   "DstATop",    "Xor",
      "PlusLighter", "Modulate",  "Screen",     "Overlay",   "Darken",     "Lighten",
      "ColorDodge",  "ColorBurn", "HardLight",  "SoftLight", "Difference", "Exclusion",
      "Multiply",    "Hue",       "Saturation", "Color",     "Luminosity", "PlusDarker"};
  if (mode < 0 || mode >= 30) {
    return nullptr;
  }
  return kNames[mode];
}

// The xp support text with only the baked mode's function: the four helpers, the mode (and its
// coverage composite), a one-line dispatcher and a one-branch macro, plus the untouched xp tail
// that follows the macro in the pack text. Any anchor that is missing leaves the full text alone.
std::string ComposeXpSupport(const std::string& full, int blendMode, bool wantsCoverage) {
  const char* modeName = BlendModeFunctionName(blendMode);
  if (modeName == nullptr) {
    return {};
  }
  const std::string macroHead = "#define XP_COMPOSITE_WITH_COVERAGE(ADVANCED_BLEND) \\";
  const auto macroAt = full.find(macroHead);
  if (macroAt == std::string::npos) {
    return {};
  }
  // The macro ends at the first line that does not continue.
  size_t cursor = macroAt;
  while (cursor < full.size()) {
    size_t eol = full.find('\n', cursor);
    if (eol == std::string::npos) {
      eol = full.size();
    }
    const bool continues = eol > cursor && full[eol - 1] == '\\';
    cursor = eol + 1;
    if (!continues) {
      break;
    }
  }
  const std::string tail = full.substr(cursor);
  std::string out = {};
  for (const char* helperName :
       {"xpLuminance", "xpSaturation", "xpSetLuminance", "xpSetSaturation"}) {
    const char* source = FindChainFunctionSource(helperName);
    if (source == nullptr) {
      return {};
    }
    out += source;
    out.push_back('\n');
  }
  const std::string modeFunction = std::string("xpBlend_") + modeName;
  const char* modeSource = FindChainFunctionSource(modeFunction.c_str());
  if (modeSource == nullptr) {
    return {};
  }
  out += modeSource;
  out.push_back('\n');
  if (wantsCoverage) {
    const std::string coverageFunction = modeFunction + "_coverage";
    const char* coverageSource = FindChainFunctionSource(coverageFunction.c_str());
    if (coverageSource == nullptr) {
      return {};
    }
    out += coverageSource;
    out.push_back('\n');
  }
  out +=
      "vec4 xpBlendColors(vec4 S, vec4 D, int mode) {\n  vec4 dstSrcAlpha = D * S.a;\n"
      "  vec4 srcDstAlpha = S * D.a;\n  return " +
      modeFunction + "(S, D, dstSrcAlpha, srcDstAlpha);\n}\n";
  if (wantsCoverage) {
    out += "#define XP_COMPOSITE_WITH_COVERAGE(ADVANCED_BLEND) return " + modeFunction +
           "_coverage(S, D, C);\n";
  } else {
    out +=
        "#define XP_COMPOSITE_WITH_COVERAGE(ADVANCED_BLEND) vec4 blended = ADVANCED_BLEND; "
        "return C * blended + (vec4(1.0) - C) * D;\n";
  }
  out += tail;
  return out;
}

}  // namespace

bool PrintChainProgramGLSL(const ChainProgramPlan& plan, const PrecompiledShaderCache* cache,
                           std::string* code, bool esProfile, int blendMode) {
  if (cache == nullptr || code == nullptr) {
    return false;
  }

  // The declarations, from the plan's tables in declaration order.
  std::string declarations = "layout(std140) uniform FragmentUniformBlock {\n";
  for (const auto& member : plan.uniforms) {
    declarations += "  ";
    declarations += GlslTypeOf(member.type);
    declarations += " ";
    declarations += member.name;
    if (member.count > 1) {
      declarations += "[" + std::to_string(member.count) + "]";
    }
    declarations += ";\n";
  }
  declarations += "};\n";
  for (const auto& varying : plan.varyings) {
    declarations += " in ";
    declarations += GlslTypeOf(varying.type);
    declarations += " " + varying.name + ";\n";
  }
  for (const auto& sampler : plan.samplers) {
    declarations += " uniform sampler2D " + sampler.name + ";\n";
  }
  // The leaf fetch's index dispatch names all four leaf samplers statically (the driver folds the
  // branch, not the text), so an assembly that samples any leaf declares the whole set.
  bool needsLeafSamplers = false;
  for (const auto& call : plan.calls) {
    if (call.op == ChainISA::Texture || call.op == ChainISA::TexModulate) {
      needsLeafSamplers = true;
    }
  }
  if (needsLeafSamplers) {
    for (int leafIndex = 0; leafIndex < 4; ++leafIndex) {
      const std::string name = "TextureSampler_" + std::to_string(leafIndex);
      bool declared = false;
      for (const auto& sampler : plan.samplers) {
        if (sampler.name == name) {
          declared = true;
        }
      }
      if (!declared) {
        declarations += " uniform sampler2D " + name + ";\n";
      }
    }
  }
  if (esProfile) {
    // ES 3.00 takes no location qualifier on the fragment output; the name binds it.
    declarations += "out vec4 fragColor;\n";
  } else {
    declarations += "layout(location = 0) out vec4 fragColor;\n";
  }

  // The declared varying whose name ends with the suffix, or empty: the per-variant spelling of a
  // geometry-owned varying (the module prefix rides the stored declaration).
  const auto varyingByNameSuffix = [&plan](const std::string& suffix) -> std::string {
    for (const auto& varying : plan.varyings) {
      if (varying.name.size() > suffix.size() &&
          varying.name.compare(varying.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return varying.name;
      }
    }
    return {};
  };
  // A per-pixel-coverage geometry spells its edge formula into the unit coverage source: the
  // function comes from the catalog (geometry/*_coverage.inc), its arguments are the varyings and
  // ChainColorParams switches the walk registered, and the call is the source's own body — the
  // shape is known here, so no value needs to live in a global the way the interpreter's generic
  // text needed one. The rect layouts carry the vCoverage varying instead.
  std::string geometryFormulas = {};
  std::string unitCoverageExpr = varyingByNameSuffix("_vCoverage");
  if (unitCoverageExpr.empty()) {
    // A geometry with no coverage varying supplies the constant: the atlas layout's glyph mask
    // reaches the composite through the clip-coverage register instead.
    unitCoverageExpr = "vec4(1.0)";
  }
  if (const auto* coverage = ChainGeometryCoverageOf(plan.geometry)) {
#if defined(TGFX_HAS_CHAIN_CATALOG)
    if (FindChainFunctionSource(coverage->function) == nullptr) {
      return false;
    }
    std::string callArgs = {};
    for (size_t arg = 0; arg < coverage->argCount; ++arg) {
      if (arg != 0) {
        callArgs += ", ";
      }
      callArgs += coverage->args[arg].name;
    }
    unitCoverageExpr = std::string(coverage->function) + "(" + callArgs + ")";
    std::set<std::string> geometryEmitted = {};
    AppendCatalogFunction(coverage->function, &geometryEmitted, &geometryFormulas);
#else
    return false;
#endif
  }

  // The source functions the body calls, each reading the members and varyings it declares. The
  // tail's final-coverage expression reads the unit coverage source whenever no coverage subtree
  // replaced it (the walk already folded that fact into the flag).
  std::string sources = {};
  // The per-vertex color varying's declared name: bare on the rect layouts, module-prefixed on
  // the per-pixel geometries (the plan registered it under the same name).
  std::string colorVarying = "vColor";
  for (const auto& varying : plan.varyings) {
    if (IsColorVaryingName(varying.name)) {
      colorVarying = varying.name;
    }
  }
  const std::string colorExpr =
      plan.hasColorVarying
          ? "(ChainColorParams_HasCommonColor != 0) ? ChainColorParams_Color : " + colorVarying
          : "ChainColorParams_Color";
  if (plan.geomColorUsed) {
    sources += "vec4 chainGeomColorSource() {\n  return " + colorExpr + ";\n}\n\n";
  }
  if (plan.opaqueColorUsed) {
    sources += "vec4 chainOpaqueGeomColorSource() {\n  vec4 color = " + colorExpr +
               ";\n  return vec4(color.rgb, 1.0);\n}\n\n";
  }
  if (plan.unitCoverageUsed) {
    sources += "vec4 chainUnitCoverageSource() {\n  return vec4(" + unitCoverageExpr + ");\n}\n\n";
  }
  if (plan.xpNeedsBlendPass) {
    const std::string modulation = plan.coverageRootRegister >= 0 ? "1" : "0";
    sources += "vec4 chainXPSrcUnpremul(vec4 result, vec4 coverageRootRegister) {\n  return (" +
               modulation + " != 0 ? result * coverageRootRegister : result);\n}\n";
  }

  // The used ops' pack entries, each at most once: the pure fragments and the shared dependencies
  // the coverage fragments call into.
  const bool needsTexture = !plan.leafSlots.empty();
  const bool needsAnalytic = plan.usedOps.count(ChainISA::LocalRectCoverage) != 0 ||
                             plan.usedOps.count(ChainISA::RRectCoverage) != 0;
  std::string xpSupport = {};
  if (plan.hasXp == 1) {
    xpSupport = ChainPackText(cache, FragmentPackXpDstTexture);
  } else if (plan.hasXp == 2) {
    xpSupport = ChainPackText(cache, FragmentPackXpFramebufferFetch);
  }
  if (plan.hasXp != 0 && xpSupport.empty()) {
    return false;  // the xfer leg's support entry is missing; nothing to spell
  }
  std::string pieces = {};
  auto appendPiece = [&](uint32_t id) {
    const std::string text = ChainPackText(cache, id);
    if (text.empty()) {
      return false;
    }
    pieces += text + "\n";
    return true;
  };
  // The sampling text includes the read-swizzle helper itself, so the standalone swizzle entry is
  // never appended: its copy only lengthened the text the driver parsed.
  const bool xpCarriesSampling = xpSupport.find("aotTextureSample(") != std::string::npos;
  if (needsTexture && !xpCarriesSampling && !appendPiece(FragmentPackDepTextureSampling)) {
    return false;
  }
  if (needsAnalytic && !appendPiece(FragmentPackDepAnalyticClipMath)) {
    return false;
  }
  if (!xpSupport.empty()) {
    const std::string composed = ComposeXpSupport(xpSupport, blendMode, plan.hasXp == 1);
    pieces += (composed.empty() ? xpSupport : composed) + "\n";
  }
#if defined(TGFX_HAS_CHAIN_CATALOG)
  std::set<std::string> catalogEmitted = {};
  // A shader-tiled leaf's fetch maps through the tiled catalog functions: the map and the border
  // pass always, the seam blend with them.
  if (!plan.tiledLeafSlots.empty()) {
    AppendCatalogFunction("tiledMapCoord", &catalogEmitted, &pieces);
    AppendCatalogFunction("tiledApplyBorder", &catalogEmitted, &pieces);
    AppendCatalogFunction("tiledSeamBlend", &catalogEmitted, &pieces);
  }
#endif
  for (int op : plan.usedOps) {
    const auto* def = FindFragment(op);
    if (def == nullptr || def->sourceFile == nullptr) {
      continue;
    }
    if (op == ChainISA::Blend && xpSupport.find("xpBlendColors(") != std::string::npos) {
      continue;  // the xfer tail already carries the blend table
    }
#if defined(TGFX_HAS_CHAIN_CATALOG)
    if (FindChainFunctionSource(def->functionName) != nullptr) {
      AppendCatalogFunction(def->functionName, &catalogEmitted, &pieces);
      continue;
    }
#endif
    if (!appendPiece(0x40000000u | static_cast<uint32_t>(op))) {
      return false;
    }
  }
  if (needsTexture) {
    if (!appendPiece(FragmentPackLeafFetchPlain)) {
      return false;
    }
  }

  // The prefetches: one per texture leaf the composition samples, in slot order. A tiled leaf's
  // coordinate maps through the tiled catalog functions with its own recipe members, and its
  // sampled color takes the seam blend and border passes.
  const std::string texSubsetVarying = varyingByNameSuffix("_vTexSubset");
  std::string prefetch = {};
  for (int leaf : plan.leafSlots) {
    const std::string suffix = std::to_string(leaf);
    const bool applyQuad = plan.hasSubset && leaf == 0;
    const std::string fetch =
        "chainLeafFetch(" +
        (plan.tiledLeafSlots.count(leaf) != 0 ? "tiledCoord_" + suffix
                                              : "TransformedCoords_" + suffix) +
        ", " + ChainLeafMemberName("Subset", leaf) + ", " +
        ChainLeafMemberName("AlphaStart", leaf) + ", " + ChainLeafMemberName("HasRgbaaa", leaf) +
        ", " + ChainLeafMemberName("SubsetClampOn", leaf) + ", " + suffix +
        ", DeviceLeafMask, DeviceLeafCoordMatrix[" + suffix + "], LeafReadSwizzles[" + suffix +
        "], gl_FragCoord.xy, " + (applyQuad ? "1" : "0") + ", " +
        (applyQuad ? texSubsetVarying : "vec4(0.0)") + ")";
    if (plan.tiledLeafSlots.count(leaf) == 0) {
      prefetch += "  chainLeafTex[" + suffix + "] = " + fetch + ";\n";
      continue;
    }
    const std::string tiled = "_" + suffix;
    prefetch += "  highp vec2 tiledIn" + tiled + ";\n";
    prefetch += "  highp vec2 tiledSubset" + tiled + ";\n";
    prefetch += "  highp vec2 tiledClamped" + tiled + ";\n";
    prefetch += "  highp vec3 tiledCoord_" + suffix + ";\n";
    prefetch += "  tiledCoord_" + suffix + " = vec3(tiledMapCoord(TransformedCoords_" + suffix +
                ", TiledStrict" + tiled + " != 0, TiledModeX" + tiled + ", TiledModeY" + tiled +
                ", TiledSubset" + tiled + ", TiledClamp" + tiled + ", TiledDimension" + tiled +
                ", tiledIn" + tiled + ", tiledSubset" + tiled + ", tiledClamped" + tiled +
                "), 1.0);\n";
    prefetch += "  chainLeafTex[" + suffix + "] = " + fetch + ";\n";
    prefetch += "  chainLeafTex[" + suffix + "] = tiledSeamBlend(TextureSampler_" + suffix +
                ", chainLeafTex[" + suffix + "], TiledModeX" + tiled + ", TiledModeY" + tiled +
                ", TiledClamp" + tiled + ", TiledDimension" + tiled + ", tiledIn" + tiled +
                ", tiledSubset" + tiled + ", tiledClamped" + tiled + ", LeafReadSwizzles[" +
                suffix + "]);\n";
    prefetch += "  chainLeafTex[" + suffix + "] = tiledApplyBorder(chainLeafTex[" + suffix +
                "], TiledModeX" + tiled + ", TiledModeY" + tiled + ", TiledSubset" + tiled +
                ", tiledIn" + tiled + ", tiledSubset" + tiled + ", tiledClamped" + tiled + ");\n";
  }

  // The straight-line body: one local per call, the registers an argument names spelled by the
  // local that wrote them, and the role assignments in the same place the walk emitted them.
  std::string body = {};
  std::unordered_map<int, std::string> registerLocals = {};
  int nextLocal = 0;
  auto spellArg = [&](const ChainArg& arg) -> std::string {
    switch (arg.kind) {
      case ChainArgKind::Register:
        return registerLocals.at(arg.index);
      case ChainArgKind::GeomColor:
        return "chainGeomColorSource()";
      case ChainArgKind::OpaqueColor:
        return "chainOpaqueGeomColorSource()";
      case ChainArgKind::UnitCoverage:
        return "chainUnitCoverageSource()";
      case ChainArgKind::White:
        return "vec4(1.0)";
      case ChainArgKind::Black:
        return "vec4(0.0)";
      case ChainArgKind::LeafTex:
        return "chainLeafTex[" + std::to_string(arg.index) + "]";
      case ChainArgKind::FragCoord:
        return "gl_FragCoord.xy";
      case ChainArgKind::Uniform:
        return plan.uniforms[static_cast<size_t>(arg.index)].name;
      case ChainArgKind::Literal:
        return std::to_string(arg.index);
    }
    return {};
  };
  for (const auto& call : plan.calls) {
    const std::string local = "cg" + std::to_string(nextLocal);
    ++nextLocal;
    const auto& a = call.args;
    std::string expr = {};
    switch (call.op) {
      case ChainISA::ConstColor:
        expr = "pmApplyConstColor(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ", " +
               spellArg(a[2]) + ")";
        break;
      case ChainISA::ColorMatrix:
        expr = "pmApplyColorMatrix(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ", " +
               spellArg(a[2]) + ")";
        break;
      case ChainISA::Luma:
        expr = "pmApplyLuma(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ")";
        break;
      case ChainISA::AlphaThreshold:
        expr = "pmApplyAlphaThreshold(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ")";
        break;
      case ChainISA::Blend:
        expr =
            "xpBlendColors(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ", " + spellArg(a[2]) + ")";
        break;
      case ChainISA::Texture:
        expr = "tgfxOpTexture(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ", " + spellArg(a[2]) +
               ", " + spellArg(a[3]) + ")";
        break;
      case ChainISA::TexModulate:
        expr = "tgfxOpTexModulate(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ", " +
               spellArg(a[2]) + ")";
        break;
      case ChainISA::InputOpaque:
        expr = "vec4(" + spellArg(a[0]) + ".rgb, 1.0)";
        break;
      case ChainISA::MulAlpha:
        expr = spellArg(a[0]) + " * " + spellArg(a[1]) + ".a";
        break;
      case ChainISA::AARectCoverage:
        expr = "tgfxOpAARectCoverage(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ", " +
               spellArg(a[2]) + ")";
        break;
      case ChainISA::LocalRectCoverage:
        expr = "tgfxOpLocalRectCoverage(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ", " +
               spellArg(a[2]) + ", " + spellArg(a[3]) + ")";
        break;
      case ChainISA::RRectCoverage:
        expr = "tgfxOpRRectCoverage(" + spellArg(a[0]) + ", " + spellArg(a[1]) + ", " +
               spellArg(a[2]) + ", " + spellArg(a[3]) + ", " + spellArg(a[4]) + ", " +
               spellArg(a[5]) + ", " + spellArg(a[6]) + ")";
        break;
      case ChainISA::ColorSpaceXform:
        expr = "chainApplyColorSpaceXform(" + spellArg(a[0]) +
               ", ChainFlags, ChainSrcTF0, ChainSrcTF1, ChainSrcTFType, ChainSrcOOTF, "
               "ChainColorXform, ChainDstOOTF, ChainDstTF0, ChainDstTF1, ChainDstTFType)";
        break;
      case ChainISA::Gradient: {
        // The evaluator's full parameter table: the block members in declaration order. The LUT
        // leaf exists only on the LUT form's block; the plain form passes 0.
        bool hasLutLeaf = false;
        for (const auto& member : plan.uniforms) {
          if (member.source == ChainBindingSource::GradientLutLeaf) {
            hasLutLeaf = true;
          }
        }
        std::string args = spellArg(a[0]) + ", SourceCoords, GradientLayoutType, " +
                           "GradientColorizerKind, " +
                           (hasLutLeaf ? std::string("GradientLutLeaf") : std::string("0"));
        for (const char* member :
             {"GradientLeftBorder", "GradientRightBorder", "GradientStart", "GradientEnd",
              "GradientScale01", "GradientBias01", "GradientScale23", "GradientBias23",
              "GradientThreshold", "GradientBias", "GradientScale", "GradientThresholds1_7",
              "GradientThresholds9_13"}) {
          args += ", ";
          args += member;
        }
        args += ", GradientScales, GradientBiases";
        expr = "evalGradientSlot(" + args + ")";
        break;
      }
      case ChainISA::PerlinNoise:
        expr =
            "evalNoiseSlot(SourceCoords, NoiseFrequencyStitch, NoiseModeOctaves, TextureSampler_0, "
            "TextureSampler_1, TextureSampler_2, TextureSampler_3)";
        break;
      case ChainISA::YUV: {
        // The multi-plane source: the three planes ride the leaf samplers at the call's own slot
        // and the two above it (the LeafTex argument carries the slot), and the conversion facts
        // are the members the walk registered from the slot's record.
        const std::string plane = std::to_string(a[1].index);
        expr = "pmApplyYUV(" + spellArg(a[0]) + ", SourceCoords, YuvSubset, YuvColorConversion, " +
               "YuvLimitedRange, YuvAlphaStart, YuvHasRgbaaa, YuvFormat, TextureSampler_" + plane +
               ", TextureSampler_" + std::to_string(a[1].index + 1) + ", TextureSampler_" +
               std::to_string(a[1].index + 2) + ")";
        break;
      }
      default:
        return false;
    }
    body += "  vec4 " + local + " = " + expr + ";\n";
    if (call.multiplyInputAlpha) {
      body += "  " + local + " *= chainGeomColorSource().a;\n";
    }
    registerLocals[call.outRegister] = local;
    if (plan.rootRegister == call.outRegister) {
      body += "  result = " + local + ";\n";
    } else if (plan.clipCoverageRegister == call.outRegister) {
      body += "  clipRegister = " + local + ";\n";
    } else if (plan.coverageRootRegister == call.outRegister) {
      body += "  coverageRootRegister = " + local + ";\n";
    }
  }

  // The tail's placeholders: the clip coverage, the final coverage before the clip folds in, the
  // device mask and the output statement.
  const std::string clip = plan.clipCoverageRegister >= 0 ? "clipRegister" : "vec4(1.0)";
  const std::string coverage =
      plan.coverageRootRegister >= 0 ? "coverageRootRegister" : "chainUnitCoverageSource()";
  std::string deviceMaskText = {};
  if (plan.hasMaskSampler) {
    if (plan.hasLeafSamplers) {
      deviceMaskText =
          "  if (HasMaskTexture != 0) {\n"
          "    highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);\n"
          "    finalCoverage *= texture(MaskTextureSampler, maskCoord.xy).r;\n"
          "  }";
    } else {
      deviceMaskText =
          "  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);\n"
          "  finalCoverage *= texture(MaskTextureSampler, maskCoord.xy).r;";
    }
  }
  std::string output = {};
  if (plan.hasXp != 0 && plan.xpNeedsBlendPass) {
    output = "  fragColor = xpCompositeBlendPass(result, chainXPDst, finalCoverage, xpBlended);";
  } else if (plan.hasXp != 0) {
    output =
        "  fragColor = applyPorterDuffXP(result, finalCoverage, gl_FragCoord.xy, "
        "DstTextureUpperLeft, DstTextureCoordScale, XPBlendMode, DstTextureSampler);";
  } else {
    output = "  fragColor = (result * finalCoverage);";
  }
  // The epilogue is a call: the tail form is a catalog function and the coverage left-fold stays
  // here as plain statements, so no placeholder template survives.
  const bool porterDuff = plan.hasXp != 0 && !plan.xpNeedsBlendPass;
  const char* tailName = porterDuff ? "chainTail_porterDuff" : "chainTail_plain";
  const char* tailSource = FindChainFunctionSource(tailName);
  if (tailSource == nullptr) {
    return false;
  }
  pieces += tailSource;
  pieces.push_back('\n');
  std::string tailText = "  vec4 finalCoverage = " + coverage + ";\n";
  if (plan.hasMaskSampler) {
    tailText += "  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);\n";
    if (plan.hasLeafSamplers) {
      tailText += "  if (HasMaskTexture != 0) {\n";
      tailText += "    finalCoverage *= texture(MaskTextureSampler, maskCoord.xy).r;\n  }\n";
    } else {
      tailText += "  finalCoverage *= texture(MaskTextureSampler, maskCoord.xy).r;\n";
    }
  }
  tailText += "  finalCoverage *= " + clip + ";\n";
  if (plan.hasXp != 0 && plan.xpNeedsBlendPass) {
    tailText +=
        "  fragColor = xpCompositeBlendPass(result, chainXPDst, finalCoverage, xpBlended);\n";
  } else if (porterDuff) {
    tailText += "  fragColor = chainTail_porterDuff(result, finalCoverage);\n";
  } else {
    tailText += "  fragColor = chainTail_plain(result, finalCoverage);\n";
  }
  tailText += "  if (OutputAlphaSwizzle != 0) {\n    fragColor = fragColor.aaaa;\n  }\n";

  std::string xpGlobals = {};
  std::string xpLocals = {};
  if (plan.hasXp != 0) {
    xpGlobals = "vec4 chainXPDst = vec4(0.0);\nvec4 chainXPSrc = vec4(0.0);\n";
    xpLocals = "  chainXPDst = xpReadDstColor();\n  vec4 xpBlended = vec4(0.0);\n";
  }
  const std::string blendPass =
      "  chainXPSrc = chainXPSrcUnpremul(result, coverageRootRegister);\n"
      "  xpBlended = xpBlendColors(chainXPSrc, chainXPDst, XPBlendMode);\n";
  const std::string versionLine =
      esProfile ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
                : "#version 330\n";
  *code = versionLine + "#define TGFX_DECLARED_CHAIN 1\n" +
          // The catalog functions' own branches (the LUT gradient's sampler read) key on NTEX:
          // the count is a shape fact here, spelled ahead of every function the text carries.
          "#define NTEX " + std::to_string(plan.leafSlots.size()) + "\n" + declarations + "\n" +
          geometryFormulas + sources + pieces + "vec4 chainLeafTex[4];\n" + xpGlobals +
          "\nvoid main() {\n"
          "  vec4 result = vec4(0.0);\n"
          "  vec4 clipRegister = vec4(0.0);\n"
          "  vec4 coverageRootRegister = vec4(0.0);\n" +
          xpLocals + prefetch + body + (plan.xpNeedsBlendPass ? blendPass : std::string()) +
          tailText + "}\n";
  return true;
}

bool EmitChainProgramGLSL(const DrawIR& ir, bool xpNeedsBlendPass, const Family& family,
                          const Variant& variant, const PrecompiledShaderCache* cache,
                          std::string* code, ChainBindings* bindings, bool esProfile,
                          int blendMode) {
  if (code == nullptr || bindings == nullptr || cache == nullptr) {
    return false;
  }
  ChainProgramPlan plan = {};
  if (!PlanChainProgramFromGraph(ir, xpNeedsBlendPass, family, variant, cache, &plan)) {
    return false;
  }
  if (!PrintChainProgramGLSL(plan, cache, code, esProfile, blendMode)) {
    return false;
  }
  *bindings = std::move(plan.bindings);
  // The dump aid: with TGFX_ASSEMBLED_DUMP pointing at a directory, every assembled text lands
  // there as assembled_N.glsl for inspection. Read-only diagnostic, no effect on the pipeline.
  if (const char* dumpDir = std::getenv("TGFX_ASSEMBLED_DUMP")) {
    static std::atomic<int> dumpCount = {0};
    const int index = dumpCount.fetch_add(1);
    std::ofstream file(std::string(dumpDir) + "/assembled_" + std::to_string(index) + ".glsl",
                       std::ios::binary);
    file << *code;
  }
  return true;
}

bool AssembleChainVertexGLSL(const ChainBindings& bindings, const std::string& storedVs,
                             std::string* code) {
  if (code == nullptr || storedVs.empty()) {
    return false;
  }
  // The stored vertex text is the variant's exact interface (attributes, the vertex block, the
  // geometry hook with its own varyings and the leaf-coordinate transforms); the emit supplies the
  // varying contract: keep a coordinate-varying declaration exactly when the composition reads it,
  // drop the stored hook's transform assignments (the interpreter's coordinate-source selection
  // does not apply — the transforms are pre-baked per leaf here), and spell the assignments the
  // composition's own leaves need ahead of the geometry hook's position write.
  const std::string varyingHead = " out vec3 TransformedCoords_";
  std::string out = {};
  size_t cursor = 0;
  while (cursor < storedVs.size()) {
    const auto lineEnd = storedVs.find('\n', cursor);
    const std::string line = storedVs.substr(
        cursor, lineEnd == std::string::npos ? std::string::npos : lineEnd - cursor + 1);
    cursor = lineEnd == std::string::npos ? storedVs.size() : lineEnd + 1;
    if (line.rfind(varyingHead, 0) == 0) {
      const int leaf = std::atoi(line.c_str() + varyingHead.size());
      if (bindings.leafVaryings.count(leaf) == 0) {
        continue;
      }
    } else if (line.rfind(" out vec3 SourceCoords;", 0) == 0 && !bindings.needsSourceCoords) {
      continue;
    } else if (line.rfind("  TransformedCoords_", 0) == 0 ||
               line.rfind("  SourceCoords = ", 0) == 0) {
      // The stored hook's own transform assignments: their coordinate source (and, on the
      // resource profiles, the transform numbering) follows the interpreter's contract, so the
      // emit rewrites every one of them below instead of keeping any.
      continue;
    }
    out += line;
  }
  std::string hookBody = {};
  for (int leaf : bindings.leafVaryings) {
    const int transform = leaf + static_cast<int>(bindings.leafTransformShift);
    hookBody += "  TransformedCoords_" + std::to_string(leaf) + " = (CoordTransformMatrix_" +
                std::to_string(transform) + " * vec3(v.localCoord, 1.0)).xyz;\n";
  }
  if (bindings.needsSourceCoords) {
    hookBody += "  SourceCoords = (CoordTransformMatrix_0 * vec3(v.localCoord, 1.0)).xyz;\n";
  }
  if (bindings.needsSourceCoords && out.find("out vec3 SourceCoords;") == std::string::npos) {
    // The stored variant carries no source-coordinate varying (a plain profile declares none),
    // but this composition reads one — the YUV source samples through it. The declaration joins
    // the text at the top level, ahead of the entry point.
    const auto insertAt = out.find("void main()");
    if (insertAt == std::string::npos) {
      return false;
    }
    out.insert(insertAt, "out vec3 SourceCoords;\n");
  }
  // The assignments land in main after the geometry hook (the leaf sources read
  // Vertex::localCoord) and before the position write.
  const auto positionAt = out.find("  gl_Position");
  if (positionAt == std::string::npos) {
    return false;
  }
  out.insert(positionAt, hookBody);

  *code = out;
  return true;
}

}  // namespace tgfx
