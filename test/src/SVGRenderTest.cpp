/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "gtest/gtest.h"
#include "tgfx/core/Data.h"
#include "tgfx/svg/xml/XMLDOM.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(SVGRenderTest, XMLParse) {

  std::string xml = R"(
    <svg width="100" height="100">
      <rect x="10" y="10" width="80" height="80" fill="red" />
    </svg>
  )";

  auto data = Data::MakeWithCopy(xml.data(), xml.size());
  EXPECT_TRUE(data != nullptr);
  auto xmlDOM = DOM::MakeFromData(*data);
  EXPECT_TRUE(xmlDOM != nullptr);

  auto rootNode = xmlDOM->getRootNode();
  EXPECT_TRUE(rootNode != nullptr);
  EXPECT_EQ(rootNode->name, "svg");

  EXPECT_EQ(static_cast<int>(rootNode->attributes.size()), 2);
  EXPECT_EQ(rootNode->attributes[0].name, "width");
  EXPECT_EQ(rootNode->attributes[0].value, "100");
  EXPECT_EQ(rootNode->attributes[1].name, "height");
  EXPECT_EQ(rootNode->attributes[1].value, "100");

  auto rectNode = rootNode->getFirstChild("");
  EXPECT_TRUE(rectNode != nullptr);
  EXPECT_EQ(rectNode->name, "rect");

  EXPECT_EQ(static_cast<int>(rectNode->attributes.size()), 5);
  EXPECT_EQ(rectNode->attributes[0].name, "x");
  EXPECT_EQ(rectNode->attributes[0].value, "10");
  EXPECT_EQ(rectNode->attributes[1].name, "y");
  EXPECT_EQ(rectNode->attributes[1].value, "10");
  EXPECT_EQ(rectNode->attributes[2].name, "width");
  EXPECT_EQ(rectNode->attributes[2].value, "80");
  EXPECT_EQ(rectNode->attributes[3].name, "height");
  EXPECT_EQ(rectNode->attributes[3].value, "80");
  EXPECT_EQ(rectNode->attributes[4].name, "fill");
  EXPECT_EQ(rectNode->attributes[4].value, "red");
}

}  // namespace tgfx