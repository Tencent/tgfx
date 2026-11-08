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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "emit/StageEmitter.h"

#include <string>
#include <utility>
#include <vector>
#include "gpu/shaders/decls/AllFragmentDecls.h"
#include "gpu/shaders/decls/ModuleDecl.h"

namespace tgfx {

namespace {

// The emitter prints declared names and paths only: every line below is a fixed template or a
// spelling taken verbatim from the declaration, so it can never grow into a small compiler.
class EntryWriter {
 public:
  explicit EntryWriter(std::string* error) : _error(error) {
  }

  bool Line(const std::string& text) {
    _text += text;
    _text += '\n';
    return true;
  }

  bool Fail(const std::string& message) {
    *_error = message;
    return false;
  }

  std::string&& Take() {
    return std::move(_text);
  }

 private:
  std::string _text;
  std::string* _error;
};

bool EmitDefines(EntryWriter* writer, const FragmentFamilyDecl& decl) {
  for (size_t index = 0; index < decl.defineCount; ++index) {
    const std::string define = decl.defines[index];
    const auto separator = define.find('=');
    if (separator == std::string::npos || separator == 0 || separator + 1 == define.size()) {
      return writer->Fail("a declared define must spell NAME=VALUE: " + define);
    }
    writer->Line("#define " + define.substr(0, separator) + " " + define.substr(separator + 1));
  }
  return true;
}

bool EmitBlock(EntryWriter* writer, const FragmentFamilyDecl& decl) {
  writer->Line(
      "layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) uniform "
      "FragmentUniformBlock {");
  for (size_t index = 0; index < decl.blockCount; ++index) {
    const auto& element = decl.block[index];
    if (element.name == nullptr) {
      return writer->Fail("a uniform-block entry has no name");
    }
    if (element.kind == FragmentElementKind::Module) {
      writer->Line(std::string("#include \"") + element.name + "\"");
      continue;
    }
    const char* glslType = GlslTypeOf(element.type);
    if (glslType == nullptr) {
      return writer->Fail(std::string("the field '") + element.name +
                          "' has a type the emitter cannot spell in GLSL");
    }
    const std::string capacity = element.capacity == nullptr
                                     ? std::string()
                                     : std::string("[") + element.capacity + "]";
    writer->Line(std::string("  ") + glslType + " " + element.name + capacity + ";");
  }
  writer->Line("};");
  return true;
}

std::string LocationText(const VaryingDecl& varying) {
  if (varying.locationExpr != nullptr) {
    return varying.locationExpr;
  }
  return std::to_string(varying.location);
}

bool EmitVarying(EntryWriter* writer, const VaryingDecl& varying) {
  const std::string declaration = std::string("layout(location = ") + LocationText(varying) +
                                  ") in " + varying.glslType + " " + varying.name + ";";
  if (varying.condition == nullptr) {
    return writer->Line(declaration);
  }
  writer->Line(std::string("#if ") + varying.condition);
  writer->Line(declaration);
  writer->Line("#endif");
  return true;
}

bool EmitSampler(EntryWriter* writer, const SamplerDecl& sampler) {
  const int count = sampler.count > 0 ? sampler.count : 1;
  const auto bindingText = [&sampler](int index) {
    if (sampler.bindingExpr != nullptr) {
      return std::string(sampler.bindingExpr);
    }
    return std::to_string(sampler.binding + index);
  };
  const auto nameText = [&sampler, count](int index) {
    if (count == 1) {
      return std::string(sampler.name);
    }
    return std::string(sampler.name) + std::to_string(index);
  };
  if (sampler.condition != nullptr) {
    writer->Line(std::string("#if ") + sampler.condition);
  }
  for (int index = 0; index < count; ++index) {
    writer->Line(std::string("layout(set = TGFX_TEXTURE_SET, binding = ") + bindingText(index) +
                 ") uniform sampler2D " + nameText(index) + ";");
  }
  if (sampler.condition != nullptr) {
    writer->Line("#endif");
  }
  return true;
}

bool EmitStep(EntryWriter* writer, const StepDecl& step) {
  const std::string selection =
      std::string("#define ") + step.fillMacro + " " + step.valueSymbol;
  if (step.condition == nullptr) {
    return writer->Line(selection);
  }
  writer->Line(std::string("#if ") + step.condition);
  writer->Line(selection);
  writer->Line("#endif");
  return true;
}

const SamplerDecl* FindSamplerGroup(const FragmentFamilyDecl& decl, const char* group) {
  for (size_t index = 0; index < decl.samplerCount; ++index) {
    const auto& sampler = decl.samplers[index];
    if (sampler.name != nullptr && group != nullptr && std::string(sampler.name) == group) {
      return &sampler;
    }
  }
  return nullptr;
}

bool EmitInstanceDispatch(EntryWriter* writer, const FragmentFamilyDecl& decl,
                          const InstanceDispatchDecl& dispatch) {
  const auto* group = FindSamplerGroup(decl, dispatch.samplerGroup);
  if (group == nullptr) {
    return writer->Fail(std::string("the dispatch '") + dispatch.function +
                        "' names no declared sampler group");
  }
  if (group->count < 2) {
    return writer->Fail(std::string("the dispatch '") + dispatch.function +
                        "' needs a sampler group of at least two instances");
  }
  const int count = group->count;
  const auto samplerName = [&group](int index) {
    return std::string(group->name) + std::to_string(index);
  };
  if (dispatch.condition != nullptr) {
    writer->Line(std::string("#if ") + dispatch.condition);
  }
  writer->Line(std::string(dispatch.returnType) + " " + dispatch.function + "(int leafIndex, " +
               dispatch.coordinateType + " coord, int readSwizzle) {");
  writer->Line("  vec4 color;");
  for (int index = 0; index < count - 1; ++index) {
    writer->Line(index == 0 ? "  if (leafIndex == 0) {"
                            : "  } else if (leafIndex == " + std::to_string(index) + ") {");
    writer->Line("    color = texture(" + samplerName(index) + ", coord);");
  }
  writer->Line("  } else {");
  writer->Line("    color = texture(" + samplerName(count - 1) + ", coord);");
  writer->Line("  }");
  writer->Line(dispatch.swizzleFunction == nullptr
                   ? std::string("  return color;")
                   : std::string("  return ") + dispatch.swizzleFunction + "(color, readSwizzle);");
  writer->Line("}");
  if (dispatch.condition != nullptr) {
    writer->Line("#endif");
  }
  return true;
}

std::string XpDstBindingText(int value, const char* expression) {
  if (expression != nullptr) {
    return expression;
  }
  return std::to_string(value);
}

bool EmitXpDstBinding(EntryWriter* writer, const FragmentFamilyDecl& decl) {
  if (decl.xpDstCondition == nullptr) {
    return writer->Line("#define XP_DST_TEX_BINDING " +
                        XpDstBindingText(decl.xpDstBinding, decl.xpDstBindingExpr));
  }
  if (decl.xpDstBindingConditional < 0 && decl.xpDstBindingConditionalExpr == nullptr) {
    return writer->Fail("a conditional XP dst binding needs both branches");
  }
  writer->Line(std::string("#if ") + decl.xpDstCondition);
  writer->Line("#define XP_DST_TEX_BINDING " +
               XpDstBindingText(decl.xpDstBindingConditional, decl.xpDstBindingConditionalExpr));
  writer->Line("#else");
  writer->Line("#define XP_DST_TEX_BINDING " +
               XpDstBindingText(decl.xpDstBinding, decl.xpDstBindingExpr));
  writer->Line("#endif");
  return true;
}

}  // namespace

std::string EmitFragmentEntry(const FragmentFamilyDecl& decl, std::string* error) {
  if (error == nullptr) {
    return {};
  }
  error->clear();
  if (decl.name == nullptr || decl.file == nullptr) {
    *error = "a declaration needs a family name and a file";
    return {};
  }
  EntryWriter writer(error);
  writer.Line("#version 450");
  writer.Line("// Generated by shader_build_tool (emit/StageEmitter); do not edit.");
  if (!EmitDefines(&writer, decl) || !EmitBlock(&writer, decl)) {
    return {};
  }
  writer.Line("");
  for (size_t index = 0; index < decl.varyingCount; ++index) {
    if (!EmitVarying(&writer, decl.varyings[index])) {
      return {};
    }
  }
  writer.Line("");
  for (size_t index = 0; index < decl.samplerCount; ++index) {
    if (!EmitSampler(&writer, decl.samplers[index])) {
      return {};
    }
  }
  if (!EmitXpDstBinding(&writer, decl)) {
    return {};
  }
  for (size_t index = 0; index < decl.dispatchCount; ++index) {
    if (!EmitInstanceDispatch(&writer, decl, decl.dispatches[index])) {
      return {};
    }
  }
  writer.Line("");
  for (size_t index = 0; index < decl.stepCount; ++index) {
    if (!EmitStep(&writer, decl.steps[index])) {
      return {};
    }
  }
  for (size_t index = 0; index < decl.extraIncludeCount; ++index) {
    writer.Line(std::string("#include \"") + decl.extraIncludes[index] + "\"");
  }
  writer.Line(std::string("#include \"") + decl.mainInclude + "\"");
  return writer.Take();
}

std::map<std::string, std::string> EmitDeclaredFragmentEntries(std::string* error) {
  std::map<std::string, std::string> entries;
  if (error == nullptr) {
    return entries;
  }
  error->clear();
  for (const auto* decl : AllFragmentDecls()) {
    if (decl == nullptr || decl->file == nullptr) {
      *error = "a registered declaration is empty";
      return {};
    }
    auto text = EmitFragmentEntry(*decl, error);
    if (text.empty()) {
      return {};
    }
    entries.emplace(decl->file, std::move(text));
  }
  return entries;
}

}  // namespace tgfx
