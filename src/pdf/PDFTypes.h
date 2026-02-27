/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#pragma once

#include "pdf/PDFUnion.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/WriteStream.h"
namespace tgfx {

class PDFDocumentImpl;

struct PDFIndirectReference {
  int value = -1;

  explicit operator bool() const {
    return value != -1;
  }

  bool operator==(PDFIndirectReference v) const {
    return value == v.value;
  }

  bool operator!=(PDFIndirectReference v) const {
    return value != v.value;
  }
};

class PDFObject {
 public:
  PDFObject() = default;
  virtual ~PDFObject() = default;

  PDFObject(PDFObject&&) = delete;
  PDFObject(const PDFObject&) = delete;
  PDFObject& operator=(PDFObject&&) = delete;
  PDFObject& operator=(const PDFObject&) = delete;

  /* 
   * Subclasses must implement this method to print the object to the PDF file.
   */
  virtual void emitObject(const std::shared_ptr<WriteStream>& stream) const = 0;
};

/**
 * Create a PDF array. Maximum length is 8191.
 */
class PDFArray final : public PDFObject {
 public:
  PDFArray() = default;
  ~PDFArray() override = default;

  size_t size() const;
  void reserve(size_t length);

  void appendInt(int32_t value);
  void appendColorComponent(uint8_t value);
  void appendBool(bool value);
  void appendScalar(float value);
  void appendName(const char name[]);
  void appendName(std::string name);
  void appendByteString(const char value[]);
  void appendTextString(std::string value);
  void appendObject(std::unique_ptr<PDFObject>&& object);
  void appendRef(PDFIndirectReference ref);

  void emitObject(const std::shared_ptr<WriteStream>& stream) const override;

 private:
  void append(PDFUnion&& value);

  std::vector<PDFUnion> values;
};

static inline void PDFArrayAppend(PDFArray* array, int value) {
  array->appendInt(value);
}

static inline void PDFArrayAppend(PDFArray* array, float value) {
  array->appendScalar(value);
}

template <typename T, typename... Args>
static inline void PDFArrayAppend(PDFArray* array, T value, Args... args) {
  PDFArrayAppend(array, value);
  PDFArrayAppend(array, args...);
}

static inline void PDFArrayAppend(PDFArray*) {
}

template <typename... Args>
static inline std::unique_ptr<PDFArray> MakePDFArray(Args... args) {
  std::unique_ptr<PDFArray> ret(new PDFArray());
  ret->reserve(sizeof...(Args));
  PDFArrayAppend(ret.get(), args...);
  return ret;
}

class PDFDictionary final : public PDFObject {
 public:
  static std::unique_ptr<PDFDictionary> Make();
  static std::unique_ptr<PDFDictionary> Make(std::string type);

  PDFDictionary() = default;
  explicit PDFDictionary(std::string type);
  ~PDFDictionary() override = default;

  // The PDFObject interface.
  void emitObject(const std::shared_ptr<WriteStream>& stream) const override;

  size_t size() const;
  void reserve(size_t length);

  void insertObject(const char key[], std::unique_ptr<PDFObject>&& object);
  void insertObject(std::string key, std::unique_ptr<PDFObject>&& object);
  void insertRef(const char key[], PDFIndirectReference ref);
  void insertRef(std::string key, PDFIndirectReference ref);

  void insertBool(const char key[], bool value);
  void insertInt(const char key[], int32_t value);
  void insertInt(const char key[], size_t value);
  void insertScalar(const char key[], float value);
  void insertName(const char key[], const char nameValue[]);
  void insertName(const char key[], std::string nameValue);
  void insertByteString(const char key[], const char value[]);
  void insertTextString(const char key[], const char value[]);
  void insertByteString(const char key[], std::string value);
  void insertTextString(const char key[], std::string value);
  void insertUnion(const char key[], PDFUnion&& value);

 private:
  std::vector<std::pair<PDFUnion, PDFUnion>> records;
};

enum class PDFSteamCompressionEnabled : bool {
  No = false,
  Yes = true,
  Default = Yes,
};

void PDFWriteTextString(const std::shared_ptr<WriteStream>& stream, const std::string& text);
void PDFWriteByteString(const std::shared_ptr<WriteStream>& stream, const char* bytes,
                        size_t length);

PDFIndirectReference PDFStreamOut(
    std::unique_ptr<PDFDictionary> dict, std::unique_ptr<Stream> stream, PDFDocumentImpl* doc,
    PDFSteamCompressionEnabled compress = PDFSteamCompressionEnabled::Default);

}  // namespace tgfx

namespace std {
template <>
struct hash<tgfx::PDFIndirectReference> {
  std::size_t operator()(const tgfx::PDFIndirectReference& s) const {
    return std::hash<int>()(s.value);
  }
};
}  // namespace std
