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
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <utility>
#include "core/DiagnosticCodes.h"
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gpu/aot/AOTParamRegistry.h"
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
size_t ValidateMacroNames(const std::string& shaderDir, Diagnostics* diagnostics) {
  size_t errors = 0;
  std::set<std::string> allowed;
  for (const auto& factory : ShaderRegistry::All()) {
    const auto shader = factory();
    // The domains are members of the info value, so a local keeps them alive through the loops:
    // ranging over shader->info().vertDomain.getDimensions() directly would hold a reference into
    // a destroyed temporary.
    const auto info = shader->info();
    for (const auto& dimension : info.vertDomain.getDimensions()) {
      allowed.insert(std::visit(DimensionNameVisitor(), dimension));
    }
    for (const auto& dimension : info.fragDomain.getDimensions()) {
      allowed.insert(std::visit(DimensionNameVisitor(), dimension));
    }
  }
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
  const std::regex conditionPattern(
      "^[ \\t]*#[ \\t]*(?:ifdef|ifndef|elif|if)\\b[ \\t]*(.*)$");
  const std::regex identifierPattern("([A-Za-z_][A-Za-z0-9_]*)");
  // Pass one: the defines of every file join the allowed set, because an includer sees them.
  for (const auto& file : files) {
    std::ifstream stream(file);
    std::string line;
    while (std::getline(stream, line)) {
      std::smatch match;
      if (std::regex_search(line, match, definePattern)) {
        allowed.insert(match[1].str());
      }
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
          continue;
        }
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyMacroUnknown, "", "", -1, -1,
               relative + ":" + std::to_string(lineNumber) + " conditions on '" + name +
                   "', which no family dimension or source define declares");
        ++errors;
      }
    }
  }
  return errors;
}

}  // namespace tgfx
