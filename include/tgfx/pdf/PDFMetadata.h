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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "tgfx/core/ColorSpace.h"

namespace tgfx {
class PDFArray;
struct PDFMetadata;

static constexpr float ScalarDefaultRasterDPI = 72.0f;

/**
 * Supports writing custom PDF objects. 
 * Users need to construct element node objects and their attribute lists according to their own
 * requirements. The object structure is as follows:
 *
 * ElementNode
 *   ├── children
 *       ├── ChildElementNode1
 *       ├── ChildElementNode2
 *   ├── attributes
 *       ├── owner1 [name1:value1, name2:value2, ...]
 *       ├── owner2 [name1:value1, name2:value2, ...]
 */

/**
 * PDFAttributeList is a helper class to manage the attributes of a PDF structure element node. 
 * Each attribute must have an owner (e.g. "Layout", "List", "Table", etc) and an attribute name
 * (e.g. "BBox", "RowSpan", etc.) from PDF32000_2008 14.8.5, and then a value of the proper type
 * according to the spec.
 */
class PDFAttributeList {
 public:
  PDFAttributeList();
  ~PDFAttributeList();

  /**
   * appends an integer attribute to the list.
   */
  void appendInt(const std::string& owner, const std::string& name, int value);

  /**
   * appends a float attribute to the list.
   */
  void appendFloat(const std::string& owner, const std::string& name, float value);

  /**
   * Appends a name attribute to the list. 
   * Note: this is not a string attribute, but the name of another attribute.
   */
  void appendName(const std::string& owner, const std::string& attrName, const std::string& value);

  /**
   * Appends a float array attribute to the list.
   */
  void appendFloatArray(const std::string& owner, const std::string& name,
                        const std::vector<float>& value);

  /**
   * Appends an array of node IDs (integers) to the list.
   */
  void appendNodeIdArray(const std::string& owner, const std::string& attrName,
                         const std::vector<int>& nodeIds);

 private:
  std::unique_ptr<PDFArray> attributes;

  friend class PDFTagTree;
};

/**
 * A custom class representing a PDF structure element node. It includes details such as the node 
 * type, child nodes, and associated attributes.
 */
struct PDFStructureElementNode {
  std::string typeString;
  std::vector<std::unique_ptr<PDFStructureElementNode>> children = {};
  int nodeId = 0;
  std::vector<int> additionalNodeIds = {};
  PDFAttributeList attributes = {};
  std::string alt;
  std::string lang;
};

/**
 * DateTime structure to represent date and time information. If not set, it defaults to the current
 * system time.
 */
struct DateTime {
  int16_t timeZoneMinutes = 0;  // The number of minutes that this is ahead of or behind UTC.
  uint16_t year = 1;            //!< e.g. 2025
  uint8_t month = 1;            //!< 1..12
  uint8_t dayOfWeek = 0;        //!< 0..6, 0==Sunday
  uint8_t day = 1;              //!< 1..31
  uint8_t hour = 0;             //!< 0..23
  uint8_t minute = 0;           //!< 0..59
  uint8_t second = 0;           //!< 0..59

  std::string toISO8601() const;
};

struct PDFMetadata {
  /** 
   * The document's title.
   */
  std::string title;

  /** 
   * The name of the person who created the document.
   */
  std::string author;

  /** 
   * The subject of the document.
   */
  std::string subject;

  /** 
   * Keywords associated with the document.  Commas may be used to delineate keywords within the
   * string.
   */
  std::string keywords;

  /** 
   * If the document was converted to PDF from another format, the name of the conforming product
   * that created the original document from which it was converted.
   */
  std::string creator;

  /** 
   * The product that is converting this document to PDF.
   */
  std::string producer = "TGFX/PDF";

  /**
   * The date and time the document was created. The zero default value represents an unknown/unset
   * time.
   */
  DateTime creation = {0, 0, 0, 0, 0, 0, 0, 0};

  /** 
   * The date and time the document was most recently modified. The zero default value represents
   * an unknown/unset time.
   */
  DateTime modified = {0, 0, 0, 0, 0, 0, 0, 0};

  /** 
   * The natural language of the text in the PDF. If fLang is empty, the root 
   * StructureElementNode::lang will be used (if not empty). Text not in this language should be
   * marked with StructureElementNode::lang.
   */
  std::string lang;

  /** 
   * The DPI (pixels-per-inch) at which features without native PDF support will be rasterized
   * (e.g. draw image with perspective, draw text with perspective, ...)
   * A larger DPI would create a PDF that reflects the original intent with better fidelity, but it
   * can make for larger PDF files too, which would use more memory while rendering, and it would be
   * slower to be processed or sent online or to printer.
   */
  float rasterDPI = ScalarDefaultRasterDPI;

  /** 
   * If true, include XMP metadata, a document UUID, and sRGB output intent information.  This adds
   * length to the document and makes it non-reproducable, but are necessary features for PDF/A-2b
   * conformance
   */
  bool PDFA = false;

  /** 
   * Encoding quality controls the trade-off between size and quality. By default this is set to 101
   * percent, which corresponds to lossless encoding. If this value is set to a value <= 100, and
   * the image is opaque, it will be encoded (using JPEG) with that quality setting.
   */
  int encodingQuality = 101;

  /** 
   * An optional tree of structured document tags that provide a semantic representation of the 
   * content. The caller should retain ownership.
   */
  PDFStructureElementNode* structureElementTreeRoot = nullptr;

  enum class Outline {
    None,
    StructureElementHeaders,
  };

  Outline outline = Outline::None;

  /** 
   * PDF streams may be compressed to save space. Use this to specify the desired compression vs
   * time tradeoff.
   */
  enum class CompressionLevel : int {
    Default = -1,
    None = 0,
    LowButFast = 1,
    Average = 6,
    HighButSlow = 9,
  };

  CompressionLevel compressionLevel = CompressionLevel::Default;

  /**
   * The destination color space for color conversion. When set, input colors and images will be
   * converted from their source color space to this color space before being written to the PDF.
   * This performs actual color value transformation.
   */
  std::shared_ptr<ColorSpace> dstColorSpace = nullptr;

  /**
   * The color space to assign (embed as ICC Profile) without performing any color conversion.
   * This only embeds the ICC Profile into the PDF to describe how the colors should be interpreted,
   * but does not transform any color values. Use this when colors are already in the desired color
   * space and you just need to tag them with the correct profile.
   */
  std::shared_ptr<ColorSpace> assignColorSpace = nullptr;
};

}  // namespace tgfx