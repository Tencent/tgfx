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

// The declaration language owns the entry files of the migrated families. These tests pin what
// the emitter must produce for every declaration (the block in order, the interface, the step
// selection, the include of the shared pipeline) and the properties the byte-equivalence check
// depends on: a name and a file per family, a GLSL spelling per field type, and no expression
// synthesis. The full byte comparison runs in the build (--emit-declarations + bundle cmp).

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>
#include "emit/StageEmitter.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/decls/AllFragmentDecls.h"
#include "gpu/shaders/decls/PointwiseChainDecl.h"
#include "gpu/shaders/decls/ModuleDecl.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

TEST(ModuleDeclTest, EveryDeclarationEmitsAFragmentEntry) {
  std::string error;
  auto entries = EmitDeclaredFragmentEntries(&error);
  EXPECT_TRUE(error.empty()) << error;
  EXPECT_EQ(entries.size(), AllFragmentDecls().size());
  for (const auto* decl : AllFragmentDecls()) {
    ASSERT_NE(decl, nullptr);
    ASSERT_NE(decl->name, nullptr);
    ASSERT_NE(decl->file, nullptr);
    auto found = entries.find(decl->file);
    ASSERT_NE(found, entries.end()) << decl->name;
    const auto& text = found->second;
    EXPECT_EQ(text.rfind("#version 450\n", 0), 0u) << decl->name;
    EXPECT_TRUE(Contains(text, "uniform FragmentUniformBlock {")) << decl->name;
    EXPECT_NE(decl->mainInclude, nullptr) << decl->name;
    EXPECT_TRUE(Contains(text, std::string("#include \"") + decl->mainInclude + "\""))
        << decl->name;
    EXPECT_TRUE(Contains(text, "#define XP_DST_TEX_BINDING ")) << decl->name;
  }
}

TEST(ModuleDeclTest, TheBlockKeepsTheDeclaredFieldOrder) {
  // The block order is the std140 order: a reordering changes every artifact's meaning, so the
  // emitter must print the block exactly as declared, modules included.
  std::string error;
  for (const auto* decl : AllFragmentDecls()) {
    const auto text = EmitFragmentEntry(*decl, &error);
    ASSERT_FALSE(text.empty()) << error;
    size_t cursor = 0;
    for (size_t index = 0; index < decl->blockCount; ++index) {
      const auto& element = decl->block[index];
      const std::string needle =
          element.kind == FragmentElementKind::Module
              ? std::string("#include \"") + element.name + "\""
              : std::string(" ") + GlslTypeOf(element.type) + " " + element.name + ";";
      const auto found = text.find(needle, cursor);
      ASSERT_NE(found, std::string::npos)
          << decl->name << " is missing or reordered at index " << index;
      cursor = found + needle.size();
    }
  }
}

TEST(ModuleDeclTest, ConditionalInterfaceIsWrappedInItsDimension) {
  std::string error;
  for (const auto* decl : AllFragmentDecls()) {
    const auto text = EmitFragmentEntry(*decl, &error);
    ASSERT_FALSE(text.empty()) << error;
    for (size_t index = 0; index < decl->varyingCount; ++index) {
      const auto& varying = decl->varyings[index];
      if (varying.condition == nullptr) {
        continue;
      }
      const std::string location = varying.locationExpr != nullptr
                                       ? varying.locationExpr
                                       : std::to_string(varying.location);
      const std::string block = std::string("#if ") + varying.condition +
                                "\nlayout(location = " + location + ") in " + varying.glslType +
                                " " + varying.name + ";\n#endif";
      EXPECT_TRUE(Contains(text, block)) << decl->name << " / " << varying.name;
    }
    for (size_t index = 0; index < decl->samplerCount; ++index) {
      const auto& sampler = decl->samplers[index];
      if (sampler.condition == nullptr) {
        continue;
      }
      EXPECT_TRUE(Contains(text, std::string("#if ") + sampler.condition)) << decl->name;
      const std::string instance = std::string("uniform sampler2D ") + sampler.name +
                                   (sampler.count > 1 ? "0;" : ";");
      EXPECT_TRUE(Contains(text, instance)) << decl->name;
    }
  }
}

TEST(ModuleDeclTest, StepsCarryTheSymbolNotTheValue) {
  // The numeric value lives in fill_fragment_main.inc alone; a declaration that carried a number
  // would be a second copy of the step vocabulary.
  std::string error;
  for (const auto* decl : AllFragmentDecls()) {
    const auto text = EmitFragmentEntry(*decl, &error);
    ASSERT_FALSE(text.empty()) << error;
    for (size_t index = 0; index < decl->stepCount; ++index) {
      const auto& step = decl->steps[index];
      EXPECT_TRUE(Contains(text, std::string("#define ") + step.fillMacro + " " +
                                  step.valueSymbol))
          << decl->name;
    }
  }
}

TEST(ModuleDeclTest, AnArrayFieldCarriesItsCapacity) {
  // The chain's per-slot tables are arrays; the declaration states the element count so the
  // emitter never invents a size.
  const FragmentElement block[] = {{FragmentElementKind::Field, "SlotPacked", ParamType::Int4,
                                    "CHAIN_SLOT_CAPACITY"}};
  const FragmentFamilyDecl decl = {"TestShader", "kernels/test.frag", block, 1, nullptr, 0,
                                   nullptr,    0,                   nullptr, 0, nullptr, 0,
                                   0,          -1,                  nullptr};
  std::string error;
  const auto text = EmitFragmentEntry(decl, &error);
  ASSERT_FALSE(text.empty()) << error;
  EXPECT_TRUE(text.find("ivec4 SlotPacked[CHAIN_SLOT_CAPACITY];") != std::string::npos);
}

TEST(ModuleDeclTest, AnArrayKindWithoutACapacityIsRejected) {
  // Float4Array describes an array element, not a scalar: without a declared capacity there is
  // nothing to print, and the emitter fails instead of guessing.
  const FragmentElement block[] = {{FragmentElementKind::Field, "Table", ParamType::Float4Array}};
  const FragmentFamilyDecl decl = {"TestShader", "kernels/test.frag", block, 1, nullptr, 0,
                                   nullptr,    0,                   nullptr, 0, nullptr, 0,
                                   0,          -1,                  nullptr};
  std::string error;
  EXPECT_TRUE(EmitFragmentEntry(decl, &error).empty());
  EXPECT_FALSE(error.empty());
}

TEST(ModuleDeclTest, TheChainInterfaceIsExpressible) {
  // The interpreter's interface is the language's stress test: dimension-dependent locations,
  // one group of four leaf samplers, and a conditional XP dst binding over the leaf count.
  std::string error;
  const auto text = EmitFragmentEntry(PointwiseChainDecl::Decl, &error);
  ASSERT_FALSE(text.empty()) << error;
  EXPECT_TRUE(text.find("layout(location = NTEX + 1) in vec4 vColor;") != std::string::npos);
  EXPECT_TRUE(text.find("layout(location = CHAIN_TEX_LOC_BASE + 3) in vec3 TransformedCoords_3;") !=
              std::string::npos);
  EXPECT_TRUE(text.find("#if GP_LAYOUT == 1") != std::string::npos);
  EXPECT_TRUE(text.find("layout(set = TGFX_TEXTURE_SET, binding = 0) uniform sampler2D "
                        "TextureSampler_0;") != std::string::npos);
  EXPECT_TRUE(text.find("uniform sampler2D TextureSampler_3;") != std::string::npos);
  EXPECT_TRUE(text.find("layout(set = TGFX_TEXTURE_SET, binding = NTEX) uniform sampler2D "
                        "MaskTextureSampler;") != std::string::npos);
  // With no leaves the mask owns binding 0, so the dst texture follows it (the binding conflict
  // the Metal compiler used to reject).
  EXPECT_TRUE(text.find("#define XP_DST_TEX_BINDING HAS_MASK_TEXTURE") != std::string::npos);
  EXPECT_TRUE(text.find("#define XP_DST_TEX_BINDING (NTEX + 1)") != std::string::npos);
}

TEST(ModuleDeclTest, ThePipelineTemplateAndSupportModulesComeFromTheDeclaration) {
  // The dedicated families keep the fill pipeline; the interpreter declares its own chain
  // template plus the support modules its loop needs. A family that declared neither still gets
  // the fill template, which is why the migrated entries did not change.
  std::string error;
  const auto chain = EmitFragmentEntry(PointwiseChainDecl::Decl, &error);
  ASSERT_FALSE(chain.empty()) << error;
  const std::string support = "#include \"pipeline/chain/pointwise_interface_support.inc\"";
  const std::string resource = "#include \"pipeline/chain/pointwise_resource_eval.inc\"";
  EXPECT_TRUE(chain.find(support) != std::string::npos);
  EXPECT_TRUE(chain.find(resource) != std::string::npos);
  const auto chainMain = chain.find("#include \"pipeline/chain/pointwise_fragment_main.inc\"");
  ASSERT_NE(chainMain, std::string::npos);
  EXPECT_LT(chain.find(support), chainMain);
  EXPECT_LT(chain.find(resource), chainMain);
  for (const auto* decl : AllFragmentDecls()) {
    const auto text = EmitFragmentEntry(*decl, &error);
    ASSERT_FALSE(text.empty()) << error;
    EXPECT_TRUE(text.find(std::string("#include \"") + decl->mainInclude + "\"") !=
                std::string::npos)
        << decl->name;
  }
}

TEST(ModuleDeclTest, TheInstanceDispatchMatchesTheHandwrittenForm) {
  // The generated reader must be the handwritten aotChainRead verbatim: one static pick per
  // instance, the last instance as the else arm (an out-of-range index reads it, which is the
  // handwritten behavior the migration must preserve).
  std::string error;
  const auto text = EmitFragmentEntry(PointwiseChainDecl::Decl, &error);
  ASSERT_FALSE(text.empty()) << error;
  const std::string expected =
      "#if NTEX > 0\n"
      "vec4 aotChainRead(int leafIndex, vec2 coord, int readSwizzle) {\n"
      "  vec4 color;\n"
      "  if (leafIndex == 0) {\n"
      "    color = texture(TextureSampler_0, coord);\n"
      "  } else if (leafIndex == 1) {\n"
      "    color = texture(TextureSampler_1, coord);\n"
      "  } else if (leafIndex == 2) {\n"
      "    color = texture(TextureSampler_2, coord);\n"
      "  } else {\n"
      "    color = texture(TextureSampler_3, coord);\n"
      "  }\n"
      "  return aotReadSwizzle(color, readSwizzle);\n"
      "}\n"
      "#endif\n";
  EXPECT_TRUE(text.find(expected) != std::string::npos) << text;
}

TEST(ModuleDeclTest, EveryFragmentDimensionIsReferencedByTheGeneratedText) {
  // The runtime test that pins this for handwritten families cannot read a generated entry, so the
  // declaration path checks the same property here: every fragment dimension of the shell must
  // appear in the generated text or in one of the shared modules it includes. Otherwise a variant
  // axis would compile to the same code as its neighbour.
  const std::string shaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";
  std::string error;
  for (const auto* decl : AllFragmentDecls()) {
    auto text = EmitFragmentEntry(*decl, &error);
    ASSERT_FALSE(text.empty()) << error;
    const auto appendModule = [&text, &shaderDir](const char* path) {
      std::ifstream file(shaderDir + "/" + path);
      EXPECT_TRUE(file.is_open()) << path;
      text += std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    };
    for (size_t index = 0; index < decl->blockCount; ++index) {
      const auto& element = decl->block[index];
      if (element.kind == FragmentElementKind::Module) {
        appendModule(element.name);
      }
    }
    for (size_t index = 0; index < decl->prologueIncludeCount; ++index) {
      appendModule(decl->prologueIncludes[index]);
    }
    for (size_t index = 0; index < decl->extraIncludeCount; ++index) {
      appendModule(decl->extraIncludes[index]);
    }
    const auto* info = ShaderRegistry::Find(decl->name);
    ASSERT_NE(info, nullptr) << decl->name;
    for (const auto& dimension : info->fragDomain.getDimensions()) {
      const auto defineName =
          std::visit([](const auto& dim) { return std::string(dim.defineName); }, dimension);
      EXPECT_NE(text.find(defineName), std::string::npos)
          << decl->name << " never references the dimension " << defineName;
    }
  }
}

TEST(ModuleDeclTest, TheDeclaredFamiliesExist) {
  // Every declared family must have a shell: the shell stays the registry entry, and the two
  // agree on the name. A declaration without a shell would generate an entry nothing reads.
  std::string error;
  for (const auto* decl : AllFragmentDecls()) {
    ASSERT_NE(decl, nullptr);
    EXPECT_FALSE(decl->name == nullptr || std::strlen(decl->name) == 0);
    EXPECT_FALSE(decl->file == nullptr || std::strlen(decl->file) == 0);
    EXPECT_GT(decl->blockCount, 0u);
    // The dedicated families select FILL_* steps; the interpreter selects steps through its
    // dimensions and resource profile instead.
    EXPECT_TRUE(decl->stepCount > 0 || decl->varyingCount > 0) << decl->name;
  }
}

}  // namespace tgfx
