/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <string>
#include "gpu/ir/ProgramValidation.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

ProgramDescription ValidDescription() {
  ProgramDescription description = {};
  VaryingDesc varying = {};
  varying.name = "vColor";
  varying.type = SLType::Float4;
  description.interface.varyings.push_back(varying);
  VertexCall store = {};
  store.kind = VertexCallKind::StoreVarying;
  store.dst = "vColor";
  store.src = "color";
  description.vertexCalls.push_back(store);
  VertexCall position = {};
  position.kind = VertexCallKind::StorePosition;
  position.src = "position";
  description.vertexCalls.push_back(position);
  return description;
}

}  // namespace

TEST(ProgramValidationTest, AssignsIDsAndLinksVertexWrites) {
  auto description = ValidDescription();
  std::string error;
  ASSERT_TRUE(ValidateProgramDescription(&description, &error)) << error;
  ASSERT_EQ(description.interface.varyings.size(), 1u);
  EXPECT_EQ(description.interface.varyings[0].id, 0u);
  EXPECT_EQ(description.interface.varyings[0].location, 0u);
  EXPECT_TRUE(description.interface.varyings[0].produced);
  EXPECT_EQ(description.vertexCalls[0].varying, 0u);
}

TEST(ProgramValidationTest, RejectsUnproducedVarying) {
  auto description = ValidDescription();
  description.vertexCalls.erase(description.vertexCalls.begin());
  std::string error;
  EXPECT_FALSE(ValidateProgramDescription(&description, &error));
  EXPECT_NE(error.find("never produced"), std::string::npos);
}

TEST(ProgramValidationTest, RejectsVaryingWriteTypeMismatch) {
  auto description = ValidDescription();
  description.interface.varyings[0].type = SLType::Float3;
  description.vertexCalls[0].kind = VertexCallKind::TransformVarying;
  description.vertexCalls[0].resultType = SLType::Float2;
  std::string error;
  EXPECT_FALSE(ValidateProgramDescription(&description, &error));
  EXPECT_NE(error.find("vec2"), std::string::npos);
  EXPECT_NE(error.find("vColor"), std::string::npos);
}

TEST(ProgramValidationTest, RejectsProjectingNonProjectiveVarying) {
  auto description = ValidDescription();
  FragmentInstruction project = {};
  project.kind = FragmentInstructionKind::Project;
  project.output = "perspCoord2D";
  project.src = "vColor";
  project.varying = 0;
  description.fragment.instructions.push_back(project);
  std::string error;
  EXPECT_FALSE(ValidateProgramDescription(&description, &error));
  EXPECT_NE(error.find("vec4"), std::string::npos);
}

TEST(ProgramValidationTest, RejectsReadingProjectiveVaryingAsAffine) {
  auto description = ValidDescription();
  FragmentInstruction read = {};
  read.kind = FragmentInstructionKind::ReadVarying;
  read.src = "vColor";
  read.varying = 0;
  description.fragment.instructions.push_back(read);
  std::string error;
  EXPECT_FALSE(ValidateProgramDescription(&description, &error));
  EXPECT_NE(error.find("vec2"), std::string::npos);
}

TEST(ProgramValidationTest, AcceptsProjectingProjectiveVarying) {
  auto description = ValidDescription();
  description.interface.varyings[0].type = SLType::Float3;
  description.vertexCalls[0].kind = VertexCallKind::TransformVarying;
  description.vertexCalls[0].resultType = SLType::Float3;
  FragmentInstruction project = {};
  project.kind = FragmentInstructionKind::Project;
  project.output = "perspCoord2D";
  project.src = "vColor";
  project.varying = 0;
  description.fragment.instructions.push_back(project);
  std::string error;
  EXPECT_TRUE(ValidateProgramDescription(&description, &error)) << error;
}

TEST(ProgramValidationTest, RejectsOperatorArgumentCountMismatch) {
  auto description = ValidDescription();
  FragmentInstruction call = {};
  call.kind = FragmentInstructionKind::Call;
  call.function = "pmApplyLuma";
  call.argRefs.resize(2);
  description.fragment.instructions.push_back(call);
  std::string error;
  EXPECT_FALSE(ValidateProgramDescription(&description, &error));
  EXPECT_NE(error.find("operator table"), std::string::npos);
}

TEST(ProgramValidationTest, AcceptsMatchingOperatorArgumentCount) {
  auto description = ValidDescription();
  FragmentInstruction call = {};
  call.kind = FragmentInstructionKind::Call;
  call.function = "pmApplyLuma";
  call.argRefs.resize(4);
  description.fragment.instructions.push_back(call);
  std::string error;
  EXPECT_TRUE(ValidateProgramDescription(&description, &error)) << error;
}

TEST(ProgramValidationTest, RejectsUndeclaredWrite) {
  auto description = ValidDescription();
  description.vertexCalls[0].dst = "vMissing";
  std::string error;
  EXPECT_FALSE(ValidateProgramDescription(&description, &error));
  EXPECT_NE(error.find("undeclared varying"), std::string::npos);
}

TEST(ProgramValidationTest, RejectsMissingPosition) {
  auto description = ValidDescription();
  description.vertexCalls.pop_back();
  std::string error;
  EXPECT_FALSE(ValidateProgramDescription(&description, &error));
  EXPECT_NE(error.find("clip-space position"), std::string::npos);
}

}  // namespace tgfx
