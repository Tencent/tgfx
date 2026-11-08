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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ContractChecks.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <utility>
#include "core/DiagnosticCodes.h"
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gpu/aot/bind/AOTParamRegistry.h"
#include "gpu/shaders/contract/SpecContract.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

namespace {

// Reports one violation of a contract check. Every check below records through this helper, so
// the code, the subject and the severity stay in one shape for the cli renderer and the tests.
void Report(Diagnostics* diagnostics, Severity severity, const char* code,
            const std::string& shader, const std::string& profile, int vert, int frag,
            std::string message) {
  Diagnostic diagnostic;
  diagnostic.severity = severity;
  diagnostic.layer = Layer::Verify;
  diagnostic.code = code;
  diagnostic.shader = shader;
  diagnostic.profile = profile;
  diagnostic.vertPermutationIndex = vert;
  diagnostic.fragPermutationIndex = frag;
  diagnostic.message = std::move(message);
  diagnostics->report(std::move(diagnostic));
}

bool HasUniform(const StageReflectionData& reflection, const std::string& name) {
  return std::any_of(reflection.uniforms.begin(), reflection.uniforms.end(),
                     [&name](const UniformEntry& entry) { return entry.name == name; });
}

}  // namespace

// Every shader whose fragment block carries the clip contract marker (HasClip) must declare the
// whole contract: a missing field means the onSetData writers (which reference the same
// ClipContract constants) would silently skip their upload and the clip would read stale data.
size_t ValidateClipContractFields(const std::vector<VariantData>& variants,
                                  Diagnostics* diagnostics) {
  const std::string contractFields[] = {
      ClipContract::Rect,          ClipContract::HasClip, ClipContract::LocalRect,
      ClipContract::RadiiX,        ClipContract::RadiiY,  ClipContract::AntiAlias,
      ClipContract::DeviceToLocal,
  };
  size_t errors = 0;
  for (const auto& variant : variants) {
    if (!HasUniform(variant.fragmentReflection, ClipContract::HasClip)) {
      continue;
    }
    for (const auto& field : contractFields) {
      if (!HasUniform(variant.fragmentReflection, field)) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyClipField, variant.shaderName,
               variant.profileTag, -1, static_cast<int>(variant.fragPermutationIndex),
               "carries the clip contract but is missing uniform '" + field + "'");
        ++errors;
      }
    }
  }
  return errors;
}

static bool IsIdentifierChar(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
         ch == '_';
}

static bool EndsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Names that end in "Shader" but are not tgfx shader families.
static bool IsExternalShaderName(const std::string& name) {
  return name == "SwiftShader";
}

size_t ValidateShaderSourceReferences(const std::string& shaderDir,
                                      const std::vector<std::string>& registeredShaders,
                                      Diagnostics* diagnostics) {
  namespace fs = std::filesystem;
  std::error_code code;
  std::set<std::string> existingFiles;
  std::vector<fs::path> sourcePaths;
  for (fs::recursive_directory_iterator it(shaderDir, code), end; !code && it != end;
       it.increment(code)) {
    if (!it->is_regular_file()) {
      continue;
    }
    existingFiles.insert(it->path().filename().string());
    auto extension = it->path().extension().string();
    if (extension == ".inc" || extension == ".frag" || extension == ".vert") {
      sourcePaths.push_back(it->path());
    }
  }
  if (code || sourcePaths.empty()) {
    Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, "", "", -1, -1,
           "cannot list shader sources under " + shaderDir);
    return 1;
  }
  const std::set<std::string> registered(registeredShaders.begin(), registeredShaders.end());
  const std::string fileSuffixes[] = {".inc", ".frag", ".vert"};
  size_t errors = 0;
  for (const auto& path : sourcePaths) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const auto text = buffer.str();
    const auto fileName = path.filename().string();
    size_t position = 0;
    while (position < text.size()) {
      if (!IsIdentifierChar(text[position])) {
        ++position;
        continue;
      }
      size_t tokenEnd = position;
      while (tokenEnd < text.size() && IsIdentifierChar(text[tokenEnd])) {
        ++tokenEnd;
      }
      const auto token = text.substr(position, tokenEnd - position);
      // A "<module>_uniforms.inc" or "pointwise_*.inc" mention is a placeholder, not a file name.
      const bool placeholder =
          position > 0 && (text[position - 1] == '>' || text[position - 1] == '*');
      for (const auto& suffix : fileSuffixes) {
        if (!placeholder && text.compare(tokenEnd, suffix.size(), suffix) == 0 &&
            (tokenEnd + suffix.size() >= text.size() ||
             !IsIdentifierChar(text[tokenEnd + suffix.size()])) &&
            existingFiles.count(token + suffix) == 0) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifySourceFile, "", "", -1, -1,
                 fileName + " refers to " + token + suffix + ", which does not exist under " +
                     shaderDir);
          ++errors;
        }
      }
      if (EndsWith(token, "Shader") && token.size() > 6 && token[0] >= 'A' && token[0] <= 'Z' &&
          !IsExternalShaderName(token) && registered.count(token) == 0) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifySourceFamily, "", "", -1, -1,
               fileName + " refers to shader family " + token +
                   ", which is not registered (removed or merged into another family?)");
        ++errors;
      }
      position = tokenEnd;
    }
  }
  return errors;
}

// Reflection-level invariants the runtime loader enforces on read: catching them at build time
// keeps a broken writer from shipping bundles that the loader would reject (or worse, accept with
// gaps in the sampler binding table).
size_t ValidateReflectionContracts(const std::vector<VariantData>& variants,
                                   Diagnostics* diagnostics) {
  size_t errors = 0;
  const auto maxEntries = BundleFormat::Reflection::MaxListEntries;
  const auto maxNameLength = BundleFormat::Reflection::MaxNameLength;
  for (const auto& variant : variants) {
    const StageReflectionData* stages[] = {&variant.vertexReflection, &variant.fragmentReflection};
    for (const auto* reflection : stages) {
      // The bundle writer serializes name lengths and per-stage entry counts as uint8
      // (BundleWriter's WriteU8 sites): values over the format's byte would wrap silently (a
      // 256-byte name is written with length 0), so reject them here at build time instead.
      if (reflection->uniforms.size() > maxEntries || reflection->samplers.size() > maxEntries) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection, variant.shaderName,
               variant.profileTag, -1, -1,
               "reflection exceeds the " + std::to_string(maxEntries) +
                   "-entry serialization limit (uniforms=" +
                   std::to_string(reflection->uniforms.size()) +
                   ", samplers=" + std::to_string(reflection->samplers.size()) + ")");
        ++errors;
      }
      std::map<std::string, int> seenNames;
      for (const auto* list : {&reflection->uniforms, &reflection->samplers}) {
        bool isSamplerList = list == &reflection->samplers;
        for (const auto& entry : *list) {
          if (entry.name.find('\0') != std::string::npos) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "reflection carries an embedded NUL in a name");
            ++errors;
            continue;
          }
          if (entry.name.size() > maxNameLength) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "entry '" + entry.name + "' exceeds the " + std::to_string(maxNameLength) +
                       "-byte name serialization limit");
            ++errors;
            continue;
          }
          if (++seenNames[entry.name] > 1) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "declares duplicate name '" + entry.name + "' in one stage");
            ++errors;
          }
          if (entry.arraySize < 1 || (isSamplerList && entry.arraySize != 1)) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "entry '" + entry.name + "' has arraySize " + std::to_string(entry.arraySize) +
                       " (uniforms need >= 1, samplers need exactly 1)");
            ++errors;
          }
        }
      }
      // Ordinary texture bindings must use the generic TextureSampler_<N> pattern with N dense
      // from zero (a gap would shift every later binding on the runtime side); dedicated-purpose
      // samplers use their registered semantic names instead (SamplerContract in KernelContract.h).
      int nextSamplerIndex = 0;
      for (const auto& sampler : reflection->samplers) {
        const std::string prefix = SamplerContract::TextureSamplerPrefix;
        bool generic = sampler.name.size() > prefix.size() &&
                       sampler.name.compare(0, prefix.size(), prefix) == 0;
        if (generic) {
          auto digits = sampler.name.substr(prefix.size());
          generic = !digits.empty() && std::all_of(digits.begin(), digits.end(),
                                                   [](char ch) { return ch >= '0' && ch <= '9'; });
        }
        if (SamplerContract::IsSemanticSamplerName(sampler.name)) {
          continue;
        }
        int parsed = -1;
        if (generic) {
          parsed = std::stoi(sampler.name.substr(prefix.size()));
        }
        if (!generic) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection, variant.shaderName,
                 variant.profileTag, -1, -1,
                 "declares sampler '" + sampler.name +
                     "' (neither the TextureSampler_<N> pattern nor a registered semantic name; "
                     "see SamplerContract in KernelContract.h)");
          ++errors;
        } else if (parsed != nextSamplerIndex) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection, variant.shaderName,
                 variant.profileTag, -1, -1,
                 "generic sampler #" + std::to_string(nextSamplerIndex) + " is named '" +
                     sampler.name + "' (indices must be dense from zero)");
          ++errors;
        }
        ++nextSamplerIndex;
      }
    }
  }
  return errors;
}

size_t ValidateInlineExpansion(const std::vector<InlineExpansionRecord>& records,
                               Diagnostics* diagnostics) {
  // One record per (shader, function) with the worst variant, instead of one per variant.
  struct Violation {
    size_t copies = 0;
    size_t instructions = 0;
    size_t duplicated = 0;
    size_t variants = 0;
    uint32_t worstVariant = 0;
  };
  std::map<std::pair<std::string, std::string>, Violation> violations = {};
  size_t invalid = 0;
  for (const auto& record : records) {
    if (!record.expansion.valid) {
      // Not a policy violation: the module itself could not be analyzed, so the check could not
      // run. Reported as unverifiable rather than folded into the policy count.
      Diagnostic diagnostic;
      diagnostic.severity = Severity::Unverifiable;
      diagnostic.layer = Layer::Verify;
      diagnostic.code = DiagnosticCode::VerifyNotApplicable;
      diagnostic.shader = record.shaderName;
      diagnostic.fragPermutationIndex = static_cast<int>(record.fragPermutationIndex);
      diagnostic.message = "the SPIR-V module could not be analyzed";
      diagnostics->report(std::move(diagnostic));
      ++invalid;
      continue;
    }
    for (const auto& function : record.expansion.functions) {
      auto duplicated = DuplicatedInstructions(function);
      if (duplicated < MaxDuplicatedInstructions) {
        continue;
      }
      auto& violation = violations[{record.shaderName, function.name}];
      ++violation.variants;
      if (duplicated > violation.duplicated) {
        violation.copies = function.copies;
        violation.instructions = function.instructions;
        violation.duplicated = duplicated;
        violation.worstVariant = record.fragPermutationIndex;
      }
    }
  }
  for (const auto& entry : violations) {
    const auto& violation = entry.second;
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Error;
    diagnostic.layer = Layer::Verify;
    diagnostic.code = DiagnosticCode::VerifyInlinePolicy;
    diagnostic.shader = entry.first.first;
    diagnostic.fragPermutationIndex = static_cast<int>(violation.worstVariant);
    diagnostic.message =
        "'" + entry.first.second + "' (" + std::to_string(violation.instructions) +
        " instructions) reaches the driver " + std::to_string(violation.copies) +
        " times in frag " + std::to_string(violation.worstVariant) + " (" +
        std::to_string(violation.variants) + " variant(s)), " +
        std::to_string(violation.duplicated) + " duplicated instructions; the limit is " +
        std::to_string(MaxDuplicatedInstructions) +
        ". Funnel the call sites into one: select the operands, then call once (or run the call "
        "in a loop whose pass count is uniform data).";
    diagnostics->report(std::move(diagnostic));
  }
  return invalid + violations.size();
}


// The base name of a reflected field: the runtime's structural suffix (Subset, Subset_1) appends
// an underscore and digits to the declared name, so the reconciliation resolves both forms.
static std::string BaseFieldName(const std::string& name) {
  auto separator = name.find_last_of('_');
  if (separator == std::string::npos || separator + 1 == name.size() || separator == 0) {
    return name;
  }
  for (size_t index = separator + 1; index < name.size(); ++index) {
    if (name[index] < '0' || name[index] > '9') {
      return name;
    }
  }
  return name.substr(0, separator);
}

// The declared entry for a reflected name, resolved through the exact name first and the
// structural-suffix-stripped base second: a name like GradientThresholds1_7 is itself declared,
// while Subset_1 resolves to the declared Subset.
static const std::pair<const std::string, UniformFormat>* DeclaredEntry(
    const std::map<std::string, UniformFormat>& formats, const std::string& name) {
  auto exact = formats.find(name);
  if (exact != formats.end()) {
    return &*exact;
  }
  auto base = formats.find(BaseFieldName(name));
  if (base != formats.end()) {
    return &*base;
  }
  return nullptr;
}

// The declared-name reconciliation, both directions: every reflected uniform field must have a
// declared writer constant with a matching format (a GLSL typo would otherwise read as zero or
// stale data at runtime), and every declared constant must appear in some variant (a dead
// declaration is a drift nobody reconciles any more).
size_t ValidateFamilyParams(const std::vector<VariantData>& variants,
                            Diagnostics* diagnostics) {
  size_t errors = 0;
  const auto& formats = AllParamFormats();
  std::set<std::string> usedNames;
  // A name the tables declare twice with different formats resolves to whichever entry the table
  // kept, so the reflection comparison alone could pass on the wrong declaration; reject the
  // conflict itself instead.
  for (const auto& name : AllParamConflicts()) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyParamUndeclared, "", "", -1, -1,
           "declared parameter '" + name + "' appears with conflicting formats");
    ++errors;
  }
  // A build with no variants carries no reflection at all, so the usage direction has nothing to
  // say; the name-and-format direction below runs over the (empty) reflection and stays clean.
  if (variants.empty()) {
    return 0;
  }
  for (const auto& variant : variants) {
    const StageReflectionData* stages[] = {&variant.vertexReflection, &variant.fragmentReflection};
    for (const auto* reflection : stages) {
      for (const auto& entry : reflection->uniforms) {
        usedNames.insert(entry.name);
        usedNames.insert(BaseFieldName(entry.name));
        const auto* declared = DeclaredEntry(formats, entry.name);
        if (declared == nullptr) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyParamUndeclared,
                 variant.shaderName, variant.profileTag, -1, -1,
                 "reflects uniform '" + entry.name +
                     "', which no module or catalog constant declares");
          ++errors;
        } else if (declared->second != static_cast<UniformFormat>(entry.format)) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyParamUndeclared,
                 variant.shaderName, variant.profileTag, -1, -1,
                 "uniform '" + entry.name + "' reflects format " +
                     std::to_string(entry.format) + " but the constant '" + declared->first +
                     "' declares format " + std::to_string(static_cast<int>(declared->second)));
          ++errors;
        }
      }
    }
  }
  for (const auto& declared : formats) {
    if (usedNames.count(declared.first) == 0) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyParamUnused, "", "", -1, -1,
             "declared parameter '" + declared.first + "' is carried by no variant");
      ++errors;
    }
  }
  return errors;
}

// The define names of one permutation stage's dimensions: the GLSL conditions may reference any
// of them, so they join the allowed set of the macro-name rule.
struct DimensionNameVisitor {
  const char* operator()(const PermutationBool& dimension) const {
    return dimension.defineName;
  }
  const char* operator()(const PermutationEnum& dimension) const {
    return dimension.defineName;
  }
  const char* operator()(const PermutationInt& dimension) const {
    return dimension.defineName;
  }
};

// The macro-name rule: every identifier a preprocessor condition references must be a family
// permutation dimension, a define in the shader sources, or a whitelisted builtin. A typo
// evaluates silently to zero and compiles a wrong variant, so it fails the build instead.
// Two further invariants ride the same scan:
//   1. A kernel file must not carry guard defaults (`#ifndef X` / `#define X 0`): every build
//      prepends the variant's dimension defines before the source, so a guard is inert and only
//      drifts from the family's Dim declaration (the defaults live there now). MAX_SIGMA is the
//      one fixed compile-time constant that still uses the guard pattern, whitelisted by name.
//   2. A declared dimension no preprocessor condition ever references compiles identical code
//      for every value and only multiplies the bundle, so it fails the build as dead.
size_t ValidateMacroNames(const std::string& shaderDir, Diagnostics* diagnostics) {
  size_t errors = 0;
  std::set<std::string> declaredDimensions;
  // The dimensions whose sources this scan actually reads: the directory may carry only part of
  // the registered families (a fixture, a partial copy), and demanding references for dimensions
  // whose sources are not here would report every one of them as dead.
  std::set<std::string> checkedDimensions;
  for (const auto& factory : ShaderRegistry::All()) {
    const auto shader = factory();
    // The domains are members of the info value, so a local keeps them alive through the loops:
    // ranging over shader->info().vertDomain.getDimensions() directly would hold a reference into
    // a destroyed temporary.
    const auto info = shader->info();
    const bool present =
        std::filesystem::exists(shaderDir + "/" + info.vertexFile) ||
        std::filesystem::exists(shaderDir + "/" + info.fragmentFile);
    for (const auto& dimension : info.vertDomain.getDimensions()) {
      auto name = std::visit(DimensionNameVisitor(), dimension);
      declaredDimensions.insert(name);
      if (present) {
        checkedDimensions.insert(name);
      }
    }
    for (const auto& dimension : info.fragDomain.getDimensions()) {
      auto name = std::visit(DimensionNameVisitor(), dimension);
      declaredDimensions.insert(name);
      if (present) {
        checkedDimensions.insert(name);
      }
    }
  }
  std::set<std::string> allowed = declaredDimensions;
  allowed.insert("GL_ES");
  allowed.insert("__VERSION__");

  std::vector<std::string> files;
  for (std::filesystem::recursive_directory_iterator it(shaderDir), end; it != end; ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    const auto extension = it->path().extension().string();
    if (extension != ".inc" && extension != ".frag" && extension != ".vert") {
      continue;
    }
    files.push_back(it->path().string());
  }
  const std::regex definePattern("^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)");
  const std::regex ifndefPattern("^[ \t]*#[ \t]*ifndef[ \t]+([A-Za-z_][A-Za-z0-9_]*)");
  const std::regex conditionPattern(
      "^[ \\t]*#[ \\t]*(?:ifdef|ifndef|elif|if)\\b[ \\t]*(.*)$");
  const std::regex identifierPattern("([A-Za-z_][A-Za-z0-9_]*)");
  static const std::set<std::string> guardDefaultWhitelist = {"MAX_SIGMA"};
  std::set<std::string> usedInConditions;
  // Pass one: the defines of every file join the allowed set, because an includer sees them —
  // with the kernel-file guard-default exception explained above.
  for (const auto& file : files) {
    const auto relative = std::filesystem::relative(file, shaderDir).generic_string();
    const bool kernelFile = relative.rfind("kernels/", 0) == 0;
    std::ifstream stream(file);
    std::string line;
    std::string pendingIfndef;
    while (std::getline(stream, line)) {
      std::smatch match;
      if (std::regex_search(line, match, ifndefPattern)) {
        pendingIfndef = match[1].str();
        continue;
      }
      // Blank lines and line comments do not break the #ifndef/#define adjacency: a hand-written
      // guard may separate the two by a comment. Any other line (an #endif, another directive,
      // code) closes the guard, so a later unrelated #define of the same name is not mistaken
      // for one.
      const auto firstChar = line.find_first_not_of(" \t");
      const auto content = firstChar == std::string::npos ? std::string() : line.substr(firstChar);
      if (content.empty() || content.rfind("//", 0) == 0) {
        continue;
      }
      if (std::regex_search(line, match, definePattern)) {
        const auto name = match[1].str();
        if (kernelFile && name == pendingIfndef && !guardDefaultWhitelist.count(name)) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyMacroUnknown, "", "", -1, -1,
                 relative + " defaults '" + name +
                     "' with the #ifndef pattern. The build prepends every declared dimension's "
                     "define before the source, so a kernel guard is inert and only drifts from "
                     "the family's Dim declaration; remove the #ifndef/#define/#endif block "
                     "(whitelist: MAX_SIGMA, a fixed compile-time constant)");
          ++errors;
        } else {
          allowed.insert(name);
        }
      }
      pendingIfndef.clear();
    }
  }
  // Pass two: every identifier a condition references must be known. The TGFX_ and __ prefixes
  // are the toolchain's own macros, kept out of the per-family bookkeeping.
  for (const auto& file : files) {
    const auto relative = std::filesystem::relative(file, shaderDir).generic_string();
    std::ifstream stream(file);
    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
      ++lineNumber;
      std::smatch match;
      if (!std::regex_search(line, match, conditionPattern)) {
        continue;
      }
      std::sregex_iterator identifier(match[1].first, match[1].second, identifierPattern);
      for (std::sregex_iterator end; identifier != end; ++identifier) {
        const auto name = (*identifier)[1].str();
        if (name == "defined" || name.rfind("TGFX_", 0) == 0 || name.rfind("__", 0) == 0 ||
            allowed.count(name) > 0) {
          usedInConditions.insert(name);
          continue;
        }
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyMacroUnknown, "", "", -1, -1,
               relative + ":" + std::to_string(lineNumber) + " conditions on '" + name +
                   "', which no family dimension or source define declares");
        ++errors;
      }
    }
  }
  for (const auto& dimension : checkedDimensions) {
    if (usedInConditions.count(dimension) == 0) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyMacroUnknown, "", "", -1, -1,
             "dimension '" + dimension +
                 "' is declared by a family but no preprocessor condition references it; every "
                 "value compiles identical code, so the dimension only multiplies the bundle");
      ++errors;
    }
  }
  return errors;
}

namespace {

// The shape of one dimension: its name, its kind and its value count (the enum value names join
// the signature, because two stages agreeing on the count but not on the names would still build
// different values for the same name).
struct DimensionSignatureVisitor {
  std::string operator()(const PermutationBool& dimension) const {
    return std::string(dimension.defineName) + ":bool:2";
  }
  std::string operator()(const PermutationEnum& dimension) const {
    std::string result =
        std::string(dimension.defineName) + ":enum:" + std::to_string(dimension.valueNames.size());
    for (const auto* value : dimension.valueNames) {
      result += '|';
      result += value;
    }
    return result;
  }
  std::string operator()(const PermutationInt& dimension) const {
    return std::string(dimension.defineName) + ":int:" + std::to_string(dimension.count);
  }
};

std::string DimensionNameOf(const std::string& signature) {
  const auto separator = signature.find(':');
  return separator == std::string::npos ? signature : signature.substr(0, separator);
}

}  // namespace

// The mirrored-dimension rule: a dimension both stages declare builds one value (the vertex and
// fragment stages are compiled from the same permutation), so the two declarations must agree on
// kind and value count. The runtime MirroredDimsAgree assert catches the mismatch only when the
// two values happen to differ; a declaration that cannot produce a differing pair (same count,
// renamed values) would slip through, and the build-time check is where the author can fix it.
size_t ValidateMirroredDimensions(Diagnostics* diagnostics) {
  size_t errors = 0;
  for (const auto& factory : ShaderRegistry::All()) {
    const auto shader = factory();
    const auto info = shader->info();
    std::map<std::string, std::string> vertexDimensions;
    for (const auto& dimension : info.vertDomain.getDimensions()) {
      const auto signature = std::visit(DimensionSignatureVisitor(), dimension);
      vertexDimensions.emplace(DimensionNameOf(signature), signature);
    }
    for (const auto& dimension : info.fragDomain.getDimensions()) {
      const auto signature = std::visit(DimensionSignatureVisitor(), dimension);
      const auto name = DimensionNameOf(signature);
      const auto vertex = vertexDimensions.find(name);
      if (vertex != vertexDimensions.end() && vertex->second != signature) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyMacroUnknown, "", "", -1, -1,
               "family '" + std::string(info.name) + "' mirrors dimension '" + name +
                   "' with different declarations ('" + vertex->second + "' in the vertex "
                   "domain, '" + signature +
                   "' in the fragment domain); both stages build the same value, so kind and "
                   "value count must match");
        ++errors;
      }
    }
  }
  return errors;
}

// The pointwise dispatch rule: the shared GLSL dispatch must cover the op list. An operator needs
// an `op == OP_<NAME>` branch or a `tgfx_pointwise_exempt <Name>: <reason>` entry (the reason is
// mandatory: an exemption is a design decision, and the file is where it is written down). A
// branch or exemption naming no listed operator fails too, so renaming or removing an operator
// cannot leave a dead arm or a stale note behind.
size_t ValidatePointwiseDispatch(const std::string& shaderDir, Diagnostics* diagnostics) {
  const std::string relative = "operator/pointwise_color_dispatch.inc";
  const auto path = std::filesystem::path(shaderDir) / relative;
  std::ifstream stream(path);
  if (!stream) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1, -1,
           relative + " is missing; the pointwise interpreters include the shared dispatch");
    return 1;
  }
  const std::string text((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
  const std::regex branchPattern("op[ \\t]*==[ \\t]*OP_([A-Z0-9_]+)");
  const std::regex exemptionPattern(
      "tgfx_pointwise_exempt[ \\t]+([A-Za-z_][A-Za-z0-9_]*)[ \\t]*:[ \\t]*([^\\r\\n]*)");
  std::set<std::string> dispatched;
  for (std::sregex_iterator it(text.begin(), text.end(), branchPattern), end; it != end; ++it) {
    dispatched.insert((*it)[1].str());
  }
  std::map<std::string, std::string> exemptions;
  for (std::sregex_iterator it(text.begin(), text.end(), exemptionPattern), end; it != end; ++it) {
    exemptions.emplace((*it)[1].str(), (*it)[2].str());
  }
  size_t errors = 0;
  std::set<std::string> listed;
  for (const auto& name : Op::PointwiseOpNames()) {
    listed.insert(name);
    if (dispatched.count(Op::DefineName(name)) != 0) {
      continue;
    }
    const auto exemption = exemptions.find(name);
    if (exemption == exemptions.end()) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1,
             -1,
             relative + " has no dispatch branch for operator '" + name +
                 "' and no tgfx_pointwise_exempt entry; add a branch or state why the call "
                 "sites dispatch it");
      ++errors;
      continue;
    }
    if (exemption->second.find_first_not_of(" \t") == std::string::npos) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1,
             -1,
             "operator '" + name +
                 "' is exempt from the shared dispatch without a reason; state why on its "
                 "tgfx_pointwise_exempt line");
      ++errors;
    }
  }
  for (const auto& define : dispatched) {
    bool known = false;
    for (const auto& name : listed) {
      if (Op::DefineName(name) == define) {
        known = true;
        break;
      }
    }
    if (!known) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1,
             -1,
             "dispatch branch 'OP_" + define +
                 "' names no operator of the op list; remove the branch or restore the operator");
      ++errors;
    }
  }
  for (const auto& entry : exemptions) {
    if (listed.count(entry.first) == 0) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1,
             -1,
             "exemption for '" + entry.first +
                 "' names no operator of the op list; remove the entry");
      ++errors;
    }
  }
  return errors;
}


namespace {

// Strips // and /* */ comments, keeping newlines so a scan cannot match inside a comment.
std::string StripSourceComments(const std::string& source) {
  std::string result;
  result.reserve(source.size());
  bool lineComment = false;
  bool blockComment = false;
  for (size_t index = 0; index < source.size(); ++index) {
    const char current = source[index];
    const char next = index + 1 < source.size() ? source[index + 1] : '\0';
    if (lineComment) {
      if (current == '\n') {
        lineComment = false;
        result.push_back(current);
      }
      continue;
    }
    if (blockComment) {
      if (current == '*' && next == '/') {
        blockComment = false;
        ++index;
      } else if (current == '\n') {
        result.push_back(current);
      }
      continue;
    }
    if (current == '/' && next == '/') {
      lineComment = true;
      ++index;
      continue;
    }
    if (current == '/' && next == '*') {
      blockComment = true;
      ++index;
      continue;
    }
    result.push_back(current);
  }
  return result;
}

bool IsScanIdentifierChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

}  // namespace

// The reader rule: a file may read the SPEC_ accessors only if it declares
// `#pragma tgfx_spec_reader`. The tool expands includes before compiling, so the check runs on
// the raw sources where the file boundary still exists; a file that joins the specialization
// surface silently would change what a spec-table edit touches without any reviewer seeing it.
size_t ValidateSpecReaders(const std::string& shaderDir, Diagnostics* diagnostics) {
  size_t errors = 0;
  for (std::filesystem::recursive_directory_iterator it(shaderDir), end; it != end; ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    const auto extension = it->path().extension().string();
    if (extension != ".inc" && extension != ".frag" && extension != ".vert") {
      continue;
    }
    std::ifstream stream(it->path());
    const std::string source((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
    const auto code = StripSourceComments(source);
    bool readsSpec = false;
    for (size_t index = 0; index + 5 <= code.size(); ++index) {
      if (code.compare(index, std::strlen(SpecContract::FieldAccessorPrefix),
                     SpecContract::FieldAccessorPrefix) != 0) {
        continue;
      }
      if (index > 0 && IsScanIdentifierChar(code[index - 1])) {
        continue;
      }
      readsSpec = true;
      break;
    }
    if (!readsSpec || source.find("#pragma tgfx_spec_reader") != std::string::npos) {
      continue;
    }
    Report(diagnostics, Severity::Error, DiagnosticCode::SpecReader, "", "", -1, -1,
           std::filesystem::relative(it->path(), shaderDir).generic_string() +
               " reads the SPEC_ accessors without declaring itself a reader; add "
               "#pragma tgfx_spec_reader or move the read into a declared reader");
    ++errors;
  }
  return errors;
}

}  // namespace tgfx
