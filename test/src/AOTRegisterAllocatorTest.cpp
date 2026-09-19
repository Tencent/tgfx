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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstddef>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/AOTChainRegisterAllocator.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

// Simulates the kernel's evaluation order against an assignment: read the input registers,
// derive a value, write the output register. An allocation bug (a register recycled before its
// last read) shows up here as a wrong final value.
std::vector<int> SimulateRegisters(const std::vector<std::vector<int>>& inputs,
                                   const AOTChainRegisterAssignment& assignment,
                                   size_t registerCount) {
  std::vector<int> registers(registerCount, -1);
  std::vector<int> values(inputs.size(), 0);
  for (size_t index = 0; index < inputs.size(); ++index) {
    int value = 1;
    for (int input : inputs[index]) {
      if (input < 0) {
        continue;
      }
      value += registers[static_cast<size_t>(assignment.outRegister[static_cast<size_t>(input)])];
    }
    values[index] = value;
    if (assignment.outRegister[index] >= 0) {
      registers[static_cast<size_t>(assignment.outRegister[index])] = value;
    }
  }
  return registers;
}

// Computes the same values with unlimited storage: the ground truth the register simulation
// must reproduce at the root.
std::vector<int> ReferenceValues(const std::vector<std::vector<int>>& inputs) {
  std::vector<int> values(inputs.size(), 0);
  for (size_t index = 0; index < inputs.size(); ++index) {
    int value = 1;
    for (int input : inputs[index]) {
      if (input >= 0) {
        value += values[static_cast<size_t>(input)];
      }
    }
    values[index] = value;
  }
  return values;
}

size_t DistinctRegisters(const AOTChainRegisterAssignment& assignment) {
  std::vector<int> used = {};
  for (int reg : assignment.outRegister) {
    if (reg < 0) {
      continue;
    }
    bool found = false;
    for (int existing : used) {
      found = found || existing == reg;
    }
    if (!found) {
      used.push_back(reg);
    }
  }
  return used.size();
}

TGFX_TEST(AOTRegisterAllocatorTest, LinearChainSharesRegisters) {
  // 32 instructions, each consuming its predecessor: the recycling must fit well below the
  // instruction count.
  std::vector<std::vector<int>> inputs(32);
  for (size_t index = 0; index < inputs.size(); ++index) {
    if (index == 0) {
      inputs[index] = {-1};
    } else {
      inputs[index] = {static_cast<int>(index) - 1};
    }
  }
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, 31, -1, -1, 16, &assignment));
  EXPECT_LE(DistinctRegisters(assignment), 2u);
  EXPECT_EQ(assignment.rootRegister, assignment.outRegister[31]);
  auto registers = SimulateRegisters(inputs, assignment, 16);
  auto reference = ReferenceValues(inputs);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.rootRegister)], reference[31]);
}

TGFX_TEST(AOTRegisterAllocatorTest, ExhaustedRegistersFail) {
  // A three-way fan-in keeps all producers live at the merge, so two registers are not enough
  // (the merge itself reuses a released producer's register after reading it).
  std::vector<std::vector<int>> inputs = {{-1}, {0}, {0, 1}, {0, 1, 2}};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_FALSE(AllocateChainRegisters(inputs, 3, -1, -1, 2, &assignment));
  ASSERT_TRUE(AllocateChainRegisters(inputs, 3, -1, -1, 3, &assignment));
  auto registers = SimulateRegisters(inputs, assignment, 3);
  auto reference = ReferenceValues(inputs);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.rootRegister)], reference[3]);
  // All three producers are live together, so they hold pairwise distinct registers.
  EXPECT_NE(assignment.outRegister[0], assignment.outRegister[1]);
  EXPECT_NE(assignment.outRegister[0], assignment.outRegister[2]);
  EXPECT_NE(assignment.outRegister[1], assignment.outRegister[2]);
}

TGFX_TEST(AOTRegisterAllocatorTest, DiamondFitsTwoRegisters) {
  // A diamond's branches are live together but the merge reuses a just-consumed register, so
  // the peak live count is two, not three.
  std::vector<std::vector<int>> inputs = {{-1}, {0}, {0}, {1, 2}};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, 3, -1, -1, 2, &assignment));
  auto registers = SimulateRegisters(inputs, assignment, 2);
  auto reference = ReferenceValues(inputs);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.rootRegister)], reference[3]);
  EXPECT_NE(assignment.outRegister[1], assignment.outRegister[2]);
}

TGFX_TEST(AOTRegisterAllocatorTest, SharedInputHeldAcrossConsumers) {
  // Instruction 2 consumes both 0 and 1, so both results are live together.
  std::vector<std::vector<int>> inputs = {{-1}, {0}, {0, 1}};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, 2, -1, -1, 2, &assignment));
  auto registers = SimulateRegisters(inputs, assignment, 2);
  auto reference = ReferenceValues(inputs);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.rootRegister)], reference[2]);
}

TGFX_TEST(AOTRegisterAllocatorTest, CoverageRootSurvivesLaterInstructions) {
  // The coverage root's register must survive instructions that execute after it: the kernel
  // reads it after the loop, so a later instruction must not recycle it.
  std::vector<std::vector<int>> inputs = {{-1}, {0}, {1}, {-3}};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, 2, 3, -1, 2, &assignment));
  EXPECT_EQ(assignment.rootRegister, assignment.outRegister[2]);
  EXPECT_EQ(assignment.coverageRootRegister, assignment.outRegister[3]);
  EXPECT_NE(assignment.coverageRootRegister, assignment.rootRegister);
  auto registers = SimulateRegisters(inputs, assignment, 2);
  auto reference = ReferenceValues(inputs);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.rootRegister)], reference[2]);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.coverageRootRegister)], reference[3]);
}

TGFX_TEST(AOTRegisterAllocatorTest, ClipCoverageSurvivesLaterInstructions) {
  // The clip-coverage product's register must survive instructions that execute after it (the
  // kernel reads it after the loop, like the roots), while a dead result between the roots is
  // still recycled.
  std::vector<std::vector<int>> inputs = {{-1}, {-3}, {0}, {1}};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, 3, -1, 2, 2, &assignment));
  EXPECT_EQ(assignment.rootRegister, assignment.outRegister[3]);
  EXPECT_EQ(assignment.clipCoverageRegister, assignment.outRegister[2]);
  EXPECT_NE(assignment.clipCoverageRegister, assignment.rootRegister);
  auto registers = SimulateRegisters(inputs, assignment, 2);
  auto reference = ReferenceValues(inputs);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.rootRegister)], reference[3]);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.clipCoverageRegister)], reference[2]);
}

TGFX_TEST(AOTRegisterAllocatorTest, DeadInstructionGetsNoRegister) {
  // Instruction 2 is read by nothing and is neither root: it must not consume a register the
  // live chain could need.
  std::vector<std::vector<int>> inputs = {{-1}, {0}, {-1}};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, 1, -1, -1, 1, &assignment));
  EXPECT_EQ(assignment.outRegister[2], -1);
  EXPECT_EQ(assignment.rootRegister, assignment.outRegister[1]);
  auto registers = SimulateRegisters(inputs, assignment, 1);
  auto reference = ReferenceValues(inputs);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.rootRegister)], reference[1]);
}

TGFX_TEST(AOTRegisterAllocatorTest, SpecialNegativeInputsAreIgnored) {
  std::vector<std::vector<int>> inputs = {{-1, -3, -4, -5}};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, 0, -1, -1, 1, &assignment));
  EXPECT_EQ(assignment.outRegister[0], 0);
  EXPECT_EQ(assignment.rootRegister, 0);
}

TGFX_TEST(AOTRegisterAllocatorTest, NonTopologicalInputFails) {
  std::vector<std::vector<int>> inputs = {{1}, {-1}};
  AOTChainRegisterAssignment assignment = {};
  EXPECT_FALSE(AllocateChainRegisters(inputs, 1, -1, -1, 16, &assignment));
}

TGFX_TEST(AOTRegisterAllocatorTest, InvalidRootIndexFails) {
  std::vector<std::vector<int>> inputs = {{-1}};
  AOTChainRegisterAssignment assignment = {};
  EXPECT_FALSE(AllocateChainRegisters(inputs, 5, -1, -1, 16, &assignment));
  EXPECT_FALSE(AllocateChainRegisters(inputs, -1, 5, -1, 16, &assignment));
  EXPECT_FALSE(AllocateChainRegisters(inputs, -1, -1, -1, 16, nullptr));
}

TGFX_TEST(AOTRegisterAllocatorTest, EmptySequenceSucceeds) {
  std::vector<std::vector<int>> inputs = {};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, -1, -1, -1, 16, &assignment));
  EXPECT_TRUE(assignment.outRegister.empty());
  EXPECT_EQ(assignment.rootRegister, -1);
  EXPECT_EQ(assignment.coverageRootRegister, -1);
}

TGFX_TEST(AOTRegisterAllocatorTest, SingleRootInstruction) {
  std::vector<std::vector<int>> inputs = {{-1}};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, 0, -1, -1, 1, &assignment));
  EXPECT_EQ(assignment.outRegister[0], 0);
  EXPECT_EQ(assignment.rootRegister, 0);
}

TGFX_TEST(AOTRegisterAllocatorTest, BranchedDagMatchesReference) {
  // A wider DAG: two nested diamonds feeding a tail. The register count stays small while the
  // simulated result must match the unlimited-storage reference exactly.
  std::vector<std::vector<int>> inputs = {{-1}, {0}, {0}, {1, 2}, {3}, {3}, {4, 5}};
  AOTChainRegisterAssignment assignment = {};
  ASSERT_TRUE(AllocateChainRegisters(inputs, 6, -1, -1, 4, &assignment));
  auto registers = SimulateRegisters(inputs, assignment, 4);
  auto reference = ReferenceValues(inputs);
  EXPECT_EQ(registers[static_cast<size_t>(assignment.rootRegister)], reference[6]);
  EXPECT_LE(DistinctRegisters(assignment), 4u);
}

}  // namespace
}  // namespace tgfx
