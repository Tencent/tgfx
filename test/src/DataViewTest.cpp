/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include <cstring>
#include <filesystem>
#include <sstream>
#include "base/TGFXTest.h"
#include "gtest/gtest.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/DataView.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/UTF.h"
#include "tgfx/core/WriteStream.h"
#include "utils/TestUtils.h"

namespace tgfx {
TGFX_TEST(DataViewTest, PNGDataCheck) {
  auto stream =
      Stream::MakeFromFile(ProjectPath::Absolute("resources/apitest/test_timestretch.png"));
  ASSERT_TRUE(stream != nullptr);
  EXPECT_TRUE(stream->size() >= 14);
  Buffer buffer(14);
  EXPECT_TRUE(stream->read(buffer.data(), 14) == 14);
  auto data = DataView(buffer.bytes(), buffer.size());
  auto secondByte = data.getUint8(1);
  auto thirdByte = data.getUint8(2);
  auto fourthByte = data.getUint8(3);
  EXPECT_TRUE(secondByte == 'P' && thirdByte == 'N' && fourthByte == 'G');
  buffer.clear();
}

TGFX_TEST(DataViewTest, ReadString) {
  Buffer buffer(100);
  buffer.clear();
  std::string text = "Hello TGFX 123";
  auto dataView = DataView(buffer.bytes(), buffer.size());
  const char* textStart = text.data();
  const char* textStop = textStart + text.size();
  while (textStart < textStop) {
    auto offset = text.size() - static_cast<size_t>(textStop - textStart);
    auto unichar = UTF::NextUTF8(&textStart, textStop);
    dataView.setInt32(offset, unichar);
  }
  EXPECT_EQ(std::string((char*)buffer.bytes(), text.size()), text);
}

TGFX_TEST(DataViewTest, ReadWriteData) {
  Buffer buffer(100);
  auto dataView = DataView(buffer.bytes(), buffer.size());
  dataView.setInt8(0, 'T');
  dataView.setUint8(1, 0xFF);
  dataView.setInt16(2, 'G');
  dataView.setUint16(4, 0xFFFF);
  dataView.setInt32(6, 'F');
  dataView.setUint32(10, 0xFFFFFFFF);
  dataView.setInt64(14, 'X');
  dataView.setUint64(22, 0xFFFFFFFFFFFFFFFF);
  dataView.setFloat(30, 1.123f);
  dataView.setDouble(34, 1.0E+39);
  dataView.setBoolean(42, false);
  EXPECT_EQ(dataView.getInt8(0), 'T');
  EXPECT_EQ(dataView.getUint8(1), 0xFF);
  EXPECT_EQ(dataView.getInt16(2), 'G');
  EXPECT_EQ(dataView.getUint16(4), 0xFFFF);
  EXPECT_EQ(dataView.getInt32(6), 'F');
  EXPECT_EQ(dataView.getUint32(10), 0xFFFFFFFF);
  EXPECT_EQ(dataView.getInt64(14), 'X');
  EXPECT_EQ(dataView.getUint64(22), 0xFFFFFFFFFFFFFFFF);
  EXPECT_FLOAT_EQ(dataView.getFloat(30), 1.123f);
  EXPECT_DOUBLE_EQ(dataView.getDouble(34), 1.0E+39);
  EXPECT_EQ(dataView.getBoolean(42), false);
  buffer.clear();
  dataView.setByteOrder(ByteOrder::BigEndian);
  dataView.setUint16(0, 0x1234);
  EXPECT_TRUE(dataView.getUint16(0) == 0x1234);
  dataView.setByteOrder(ByteOrder::LittleEndian);
  EXPECT_TRUE(dataView.getUint16(0) == 0x3412);
}

TGFX_TEST(DataViewTest, MemoryWriteStream) {
  auto stream = MemoryWriteStream::Make();
  EXPECT_TRUE(stream != nullptr);
  stream->writeText("Hello");
  stream->writeText("\n");
  const char* text = "TGFX";
  stream->write(text, std::strlen(text));

  auto data = stream->readData();
  EXPECT_TRUE(data != nullptr);
  EXPECT_EQ(data->size(), 10U);
  EXPECT_EQ(std::string((char*)data->bytes(), data->size()), "Hello\nTGFX");

  std::string str(4, '\0');
  EXPECT_TRUE(stream->read(str.data(), 6, 4));
  EXPECT_EQ(str.size(), 4U);
  EXPECT_EQ(str, "TGFX");

  EXPECT_FALSE(stream->read(str.data(), 10, 10));
}

TGFX_TEST(DataViewTest, FileWriteStream) {
  auto path = ProjectPath::Absolute("test/out/FileWrite.txt");
  std::filesystem::path filePath = path;
  std::filesystem::create_directories(filePath.parent_path());

  auto writeStream = WriteStream::MakeFromFile(path);
  EXPECT_TRUE(writeStream != nullptr);
  writeStream->writeText("Hello");
  writeStream->writeText("\n");
  const char* text = "TGFX";
  writeStream->write(text, std::strlen(text));
  writeStream->flush();

  auto readStream = Stream::MakeFromFile(path);
  EXPECT_TRUE(readStream != nullptr);
  EXPECT_EQ(readStream->size(), 10U);
  Buffer buffer(readStream->size());
  readStream->read(buffer.data(), buffer.size());
  EXPECT_EQ(std::string((char*)buffer.data(), buffer.size()), "Hello\nTGFX");

  std::filesystem::remove(path);
}

}  // namespace tgfx
