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

#include "PDFMetadataUtils.h"
#include "core/utils/Log.h"
#include "core/utils/MD5.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

namespace {
constexpr DateTime ZeroTime = {0, 0, 0, 0, 0, 0, 0, 0};

bool operator!=(const DateTime& u, const DateTime& v) {
  return u.timeZoneMinutes != v.timeZoneMinutes || u.year != v.year || u.month != v.month ||
         u.dayOfWeek != v.dayOfWeek || u.day != v.day || u.hour != v.hour || u.minute != v.minute ||
         u.second != v.second;
}

std::string PDFData(const DateTime& dateTime) {
  int timeZoneMinutes = static_cast<int>(dateTime.timeZoneMinutes);
  char timezoneSign = timeZoneMinutes >= 0 ? '+' : '-';
  int timeZoneHours = std::abs(timeZoneMinutes) / 60;
  timeZoneMinutes = std::abs(timeZoneMinutes) % 60;

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "D:%04u%02u%02u%02u%02u%02u%c%02d'%02d'", dateTime.year,
           dateTime.month, dateTime.day, dateTime.hour, dateTime.minute, dateTime.second,
           timezoneSign, timeZoneHours, timeZoneMinutes);
  return std::string(buffer);
}

struct MetadataKey {
  const char* const key;
  std::string PDFMetadata::* const valuePointer;
};

const std::vector<MetadataKey> MetadataKeyList = {
    {"Title", &PDFMetadata::title},     {"Author", &PDFMetadata::author},
    {"Subject", &PDFMetadata::subject}, {"Keywords", &PDFMetadata::keywords},
    {"Creator", &PDFMetadata::creator}, {"Producer", &PDFMetadata::producer},
};
}  // namespace

std::unique_ptr<PDFObject> PDFMetadataUtils::MakeDocumentInformationDict(
    const PDFMetadata& metadata) {
  auto dict = PDFDictionary::Make();
  for (const auto [key, valuePointer] : MetadataKeyList) {
    const std::string& valueText = metadata.*(valuePointer);
    if (!valueText.empty()) {
      dict->insertTextString(key, valueText);
    }
  }
  if (metadata.creation != ZeroTime) {
    dict->insertTextString("CreationDate", PDFData(metadata.creation));
  }
  if (metadata.modified != ZeroTime) {
    dict->insertTextString("ModDate", PDFData(metadata.modified));
  }
  return dict;
}

// UUID PDFMetadataUtils::CreateUUID(const PDFMetadata& metadata) {
//   // The main requirement is for the UUID to be unique; the exact
//   // format of the data that will be hashed is not important.
//   MD5 md5;
//   const char uuidNamespace[] = "TGFX.pdf\n";
//   md5.writeText(uuidNamespace);

//   auto now = std::chrono::system_clock::now();
//   auto millisecond =
//       std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
//   md5.write(&millisecond, sizeof(millisecond));

//   DateTime dateTime;
//   PDFUtils::GetDateTime(&dateTime);
//   md5.write(&dateTime, sizeof(dateTime));
//   md5.write(&metadata.creation, sizeof(metadata.creation));
//   md5.write(&metadata.modified, sizeof(metadata.modified));

//   for (const auto [key, valuePointer] : MetadataKeyList) {
//     md5.writeText(key);
//     md5.write("\037", 1);
//     const std::string& valueText = metadata.*(valuePointer);
//     md5.writeText(valueText);
//     md5.write("\036", 1);
//   }

//   MD5::Digest digest = md5.finish();
//   // See RFC 4122, page 6-7.
//   digest.data[6] = (digest.data[6] & 0x0F) | 0x30;
//   digest.data[8] = (digest.data[6] & 0x3F) | 0x80;
//   static_assert(sizeof(digest) == sizeof(UUID), "uuid_size");
//   UUID uuid;
//   memcpy(reinterpret_cast<void*>(&uuid), &digest, sizeof(digest));
//   return uuid;
// }

UUID PDFMetadataUtils::CreateUUID(const PDFMetadata& metadata) {
  // The main requirement is for the UUID to be unique; the exact
  // format of the data that will be hashed is not important.
  // MD5 md5;
  auto md5 = MemoryWriteStream::Make();
  const char uuidNamespace[] = "TGFX.pdf\n";
  md5->writeText(uuidNamespace);

  auto now = std::chrono::system_clock::now();
  auto millisecond =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  md5->write(&millisecond, sizeof(millisecond));

  DateTime dateTime;
  PDFUtils::GetDateTime(&dateTime);
  md5->write(&dateTime, sizeof(dateTime));
  md5->write(&metadata.creation, sizeof(metadata.creation));
  md5->write(&metadata.modified, sizeof(metadata.modified));

  for (const auto [key, valuePointer] : MetadataKeyList) {
    md5->writeText(key);
    md5->write("\037", 1);
    const std::string& valueText = metadata.*(valuePointer);
    md5->writeText(valueText);
    md5->write("\036", 1);
  }
  auto data = md5->readData();

  auto digest = MD5::Calculate(data->data(), data->size());
  // See RFC 4122, page 6-7.
  digest[6] = (digest[6] & 0x0F) | 0x30;
  digest[8] = (digest[6] & 0x3F) | 0x80;
  static_assert(sizeof(digest) == sizeof(UUID), "uuid_size");
  UUID uuid;
  memcpy(reinterpret_cast<void*>(&uuid), digest.data(), sizeof(digest));
  return uuid;
}

std::unique_ptr<PDFObject> PDFMetadataUtils::MakePDFId(const UUID& doc, const UUID& instance) {
  // /ID [ <81b14aafa313db63dbd6f981e49f94f4>
  //       <81b14aafa313db63dbd6f981e49f94f4> ]
  auto array = MakePDFArray();
  static_assert(sizeof(UUID) == 16, "uuid_size");
  array->appendTextString(
      std::string(reinterpret_cast<const char*>(doc.data.data()), sizeof(UUID)));
  array->appendTextString(
      std::string(reinterpret_cast<const char*>(instance.data.data()), sizeof(UUID)));
  return array;
}

namespace {
uint32_t CountXMLEscapeSize(const std::string& input) {
  uint32_t extra = 0;
  for (char i : input) {
    if (i == '&') {
      extra += 4;  // strlen("&amp;") - strlen("&")
    } else if (i == '<') {
      extra += 3;  // strlen("&lt;") - strlen("<")
    }
  }
  return extra;
}

std::string EscapeXML(const std::string& input, const char* before = nullptr,
                      const char* after = nullptr) {
  if (input.empty()) {
    return input;
  }
  // "&" --> "&amp;" and  "<" --> "&lt;"
  // text is assumed to be in UTF-8
  // all strings are xml content, not attribute values.
  auto beforeLen = before ? strlen(before) : 0;
  auto afterLen = after ? strlen(after) : 0;
  auto extra = CountXMLEscapeSize(input);
  std::string output(input.size() + extra + beforeLen + afterLen, '\0');
  char* out = output.data();
  if (before) {
    strncpy(out, before, beforeLen);
    out += beforeLen;
  }
  constexpr char kAmp[] = "&amp;";
  constexpr char kLt[] = "&lt;";
  for (char i : input) {
    if (i == '&') {
      memcpy(out, kAmp, strlen(kAmp));
      out += strlen(kAmp);
    } else if (i == '<') {
      memcpy(out, kLt, strlen(kLt));
      out += strlen(kLt);
    } else {
      *out++ = i;
    }
  }
  if (after) {
    strncpy(out, after, afterLen);
    out += afterLen;
  }
  // Validate that we haven't written outside of our string.
  DEBUG_ASSERT(out == &output[output.size()]);
  *out = '\0';
  return output;
}

// Convert a block of memory to hexadecimal.  Input and output pointers will be
// moved to end of the range.
void Hexify(const uint8_t** inputPtr, char** outputPtr, int count) {
  DEBUG_ASSERT(inputPtr && *inputPtr);
  DEBUG_ASSERT(outputPtr && *outputPtr);
  while (count-- > 0) {
    uint8_t value = *(*inputPtr)++;
    *(*outputPtr)++ = HexadecimalDigits::lower[value >> 4];
    *(*outputPtr)++ = HexadecimalDigits::lower[value & 0xF];
  }
}

std::string UUIDToString(const UUID& uuid) {
  //  8-4-4-4-12
  char buffer[36];  // [32 + 4]
  char* ptr = buffer;
  const uint8_t* data = uuid.data.data();
  Hexify(&data, &ptr, 4);
  *ptr++ = '-';
  Hexify(&data, &ptr, 2);
  *ptr++ = '-';
  Hexify(&data, &ptr, 2);
  *ptr++ = '-';
  Hexify(&data, &ptr, 2);
  *ptr++ = '-';
  Hexify(&data, &ptr, 6);
  DEBUG_ASSERT(ptr == buffer + 36);
  DEBUG_ASSERT(data == uuid.data.data() + 16);
  return std::string(buffer, 36);
}

}  // namespace

PDFIndirectReference PDFMetadataUtils::MakeXMPObject(const PDFMetadata& metadata, const UUID& doc,
                                                     const UUID& instance,
                                                     PDFDocumentImpl* document) {
  std::string creationDate;
  std::string modificationDate;
  if (metadata.creation != ZeroTime) {
    std::string tmp = metadata.creation.toISO8601();
    DEBUG_ASSERT(0 == CountXMLEscapeSize(tmp));
    // YYYY-mm-ddTHH:MM:SS[+|-]ZZ:ZZ; no need to escape
    creationDate = "<xmp:CreateDate>" + tmp + "</xmp:CreateDate>\n";
  }
  if (metadata.modified != ZeroTime) {
    std::string tmp = metadata.modified.toISO8601();
    DEBUG_ASSERT(0 == CountXMLEscapeSize(tmp));
    modificationDate = "<xmp:ModifyDate>" + tmp + "</xmp:ModifyDate>\n";
  }
  std::string title =
      EscapeXML(metadata.title, "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">",
                "</rdf:li></rdf:Alt></dc:title>\n");

  std::string author = EscapeXML(metadata.author, "<dc:creator><rdf:Seq><rdf:li>",
                                 "</rdf:li></rdf:Seq></dc:creator>\n");
  // TODO (YGaurora): in theory, XMP can support multiple authors.  Split on a delimiter?
  std::string subject =
      EscapeXML(metadata.subject, "<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">",
                "</rdf:li></rdf:Alt></dc:description>\n");
  std::string keywords1 = EscapeXML(metadata.keywords, "<dc:subject><rdf:Bag><rdf:li>",
                                    "</rdf:li></rdf:Bag></dc:subject>\n");
  std::string keywords2 = EscapeXML(metadata.keywords, "<pdf:Keywords>", "</pdf:Keywords>\n");
  // TODO (YGaurora): in theory, keywords can be a list too.

  std::string producer = EscapeXML(metadata.producer, "<pdf:Producer>", "</pdf:Producer>\n");

  std::string creator = EscapeXML(metadata.creator, "<xmp:CreatorTool>", "</xmp:CreatorTool>\n");
  std::string documentID = UUIDToString(doc);  // no need to escape
  DEBUG_ASSERT(0 == CountXMLEscapeSize(documentID));
  std::string instanceID = UUIDToString(instance);
  DEBUG_ASSERT(0 == CountXMLEscapeSize(instanceID));

  // clang-format off
  std::string value =
      "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"\n"
      " x:xmptk=\"Adobe XMP Core 5.4-c005 78.147326, "
      "2012/08/23-13:03:03\">\n"
      "<rdf:RDF "
      "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
      "<rdf:Description rdf:about=\"\"\n"
      " xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
      " xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
      " xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\"\n"
      " xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\"\n"
      " xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\">\n"
      "<pdfaid:part>2</pdfaid:part>\n"
      "<pdfaid:conformance>B</pdfaid:conformance>\n" +
      modificationDate + 
      creationDate + 
      creator + 
      "<dc:format>application/pdf</dc:format>\n" +
      title +
      subject + 
      author + 
      keywords1 + 
      "<xmpMM:DocumentID>uuid:" + documentID + "</xmpMM:DocumentID>\n" +
      "<xmpMM:InstanceID>uuid:" + instanceID + "</xmpMM:InstanceID>\n" +
      producer + 
      keywords2 +
      "</rdf:Description>\n"
      "</rdf:RDF>\n"
      "</x:xmpmeta>\n"
      "<?xpacket end=\"w\"?>";
  // clang-format on

  auto dict = PDFDictionary::Make("Metadata");
  dict->insertName("Subtype", "XML");
  auto data = Data::MakeWithCopy(value.c_str(), value.size());
  return PDFStreamOut(std::move(dict), Stream::MakeFromData(data), document,
                      PDFSteamCompressionEnabled::No);
}

}  // namespace tgfx
