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

#include "gpu/ir/ValueRef.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(ValueTableTest, SameNameKeepsItsIDAndFillsTheType) {
  ValueTable table = {};
  const auto first = table.Intern("output", SLType::Void, ValueOrigin::CallResult);
  ASSERT_NE(first, INVALID_VALUE_ID);
  const auto second = table.Intern("output", SLType::Float4, ValueOrigin::Local);
  EXPECT_EQ(first, second);
  const auto* value = table.Find(first);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->type, SLType::Float4);
  EXPECT_EQ(value->origin, ValueOrigin::CallResult);
}

TEST(ValueTableTest, ConflictingTypesAreRejected) {
  ValueTable table = {};
  ASSERT_NE(table.Intern("value", SLType::Float2, ValueOrigin::Local), INVALID_VALUE_ID);
  EXPECT_EQ(table.Intern("value", SLType::Float3, ValueOrigin::Local), INVALID_VALUE_ID);
}

TEST(ValueTableTest, LookupByNameAndID) {
  ValueTable table = {};
  const auto id = table.Intern("vColor", SLType::Float4, ValueOrigin::Varying);
  ASSERT_NE(id, INVALID_VALUE_ID);
  const auto* byName = table.Find("vColor");
  const auto* byId = table.Find(id);
  ASSERT_NE(byName, nullptr);
  ASSERT_NE(byId, nullptr);
  EXPECT_EQ(byName->id, byId->id);
  EXPECT_EQ(byName->name, "vColor");
}

}  // namespace tgfx
