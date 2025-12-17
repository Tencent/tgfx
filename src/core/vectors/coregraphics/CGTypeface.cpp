/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "CGTypeface.h"
#include <CoreFoundation/CoreFoundation.h>
#include "CGScalerContext.h"
#include "core/AdvancedTypefaceInfo.h"
#include "core/utils/FontTableTag.h"
#include "core/utils/UniqueID.h"
#include "tgfx/core/FontStyle.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {

template <typename CFRef>
struct CFReleaseDeleter {
  void operator()(CFTypeRef ref) const {
    if (ref) {
      CFRelease(ref);
    }
  }
};

template <typename CFRef>
using UniqueCFRef = std::unique_ptr<std::remove_pointer_t<CFRef>, CFReleaseDeleter<CFRef>>;

std::string CGTypeface::StringFromCFString(CFStringRef src) {
  static const CFIndex kCStringSize = 128;
  char temporaryCString[kCStringSize];
  bzero(temporaryCString, kCStringSize);
  CFStringGetCString(src, temporaryCString, kCStringSize, kCFStringEncodingUTF8);
  return {temporaryCString};
}

std::shared_ptr<Typeface> Typeface::MakeFromName(const std::string& fontFamily,
                                                 const std::string& fontStyle) {

  CFMutableDictionaryRef cfAttributes = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  if (!fontFamily.empty()) {
    auto cfFontName =
        CFStringCreateWithCString(kCFAllocatorDefault, fontFamily.c_str(), kCFStringEncodingUTF8);
    if (cfFontName) {
      CFDictionaryAddValue(cfAttributes, kCTFontFamilyNameAttribute, cfFontName);
      CFRelease(cfFontName);
    }
  }
  if (!fontStyle.empty()) {
    auto cfStyleName =
        CFStringCreateWithCString(kCFAllocatorDefault, fontStyle.c_str(), kCFStringEncodingUTF8);
    if (cfStyleName) {
      CFDictionaryAddValue(cfAttributes, kCTFontStyleNameAttribute, cfStyleName);
      CFRelease(cfStyleName);
    }
  }
  std::shared_ptr<CGTypeface> typeface;
  auto cfDesc = CTFontDescriptorCreateWithAttributes(cfAttributes);
  if (cfDesc) {
    auto ctFont = CTFontCreateWithFontDescriptor(cfDesc, 0, nullptr);
    if (ctFont) {
      typeface = CGTypeface::Make(ctFont);
      CFRelease(ctFont);
    }
    CFRelease(cfDesc);
  }
  CFRelease(cfAttributes);
  return typeface;
}

static constexpr std::array<float, 11> FontWeightMap = {-1.0f, -0.6f, -0.5f, -0.4f, 0.0f, 0.23f,
                                                        0.3f,  0.4f,  0.56f, 0.62f, 0.7f};

static constexpr std::array<float, 9> FontWidthMap = {-0.8f, -0.6f, -0.4f, -0.2f, 0.0f,
                                                      0.2f,  0.4f,  0.6f,  0.8f};

static constexpr std::array<float, 3> FontSlantMap = {-1.0f, 0.0f, 1.0f};

std::shared_ptr<Typeface> Typeface::MakeFromName(const std::string& fontFamily,
                                                 const FontStyle& fontStyle) {

  CFMutableDictionaryRef cfAttributes = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  if (!fontFamily.empty()) {
    auto cfFontName =
        CFStringCreateWithCString(kCFAllocatorDefault, fontFamily.c_str(), kCFStringEncodingUTF8);
    if (cfFontName) {
      CFDictionaryAddValue(cfAttributes, kCTFontFamilyNameAttribute, cfFontName);
      CFRelease(cfFontName);
    }
  }

  CFMutableDictionaryRef cfTraits = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  if (cfTraits) {
    float fontWeight = FontWeightMap[static_cast<size_t>(fontStyle.weight())];
    CFNumberRef cfWeight = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &fontWeight);
    if (cfWeight) {
      CFDictionaryAddValue(cfTraits, kCTFontWeightTrait, cfWeight);
      CFRelease(cfWeight);
    }

    float fontWidth = FontWidthMap[static_cast<size_t>(fontStyle.width())];
    CFNumberRef cfWidth = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &fontWidth);
    if (cfWidth) {
      CFDictionaryAddValue(cfTraits, kCTFontWidthTrait, cfWidth);
      CFRelease(cfWidth);
    }

    float fontSlant = FontSlantMap[static_cast<size_t>(fontStyle.slant())];
    CFNumberRef cfSlant = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &fontSlant);
    if (cfSlant) {
      CFDictionaryAddValue(cfTraits, kCTFontSlantTrait, cfSlant);
      CFRelease(cfSlant);
    }

    CFDictionaryAddValue(cfAttributes, kCTFontTraitsAttribute, cfTraits);
    CFRelease(cfTraits);
  }

  std::shared_ptr<CGTypeface> typeface;
  auto cfDesc = CTFontDescriptorCreateWithAttributes(cfAttributes);
  if (cfDesc) {
    auto ctFont = CTFontCreateWithFontDescriptor(cfDesc, 0, nullptr);
    if (ctFont) {
      typeface = CGTypeface::Make(ctFont);
      CFRelease(ctFont);
    }
    CFRelease(cfDesc);
  }
  CFRelease(cfAttributes);
  return typeface;
}

std::shared_ptr<Typeface> Typeface::MakeFromPath(const std::string& fontPath, int) {
  CGDataProviderRef cgDataProvider = CGDataProviderCreateWithFilename(fontPath.c_str());
  if (cgDataProvider == nullptr) {
    return nullptr;
  }
  std::shared_ptr<CGTypeface> typeface;
  auto cgFont = CGFontCreateWithDataProvider(cgDataProvider);
  if (cgFont) {
    auto ctFont = CTFontCreateWithGraphicsFont(cgFont, 0, nullptr, nullptr);
    if (ctFont) {
      typeface = CGTypeface::Make(ctFont);
      CFRelease(ctFont);
    }
    CFRelease(cgFont);
  }
  CGDataProviderRelease(cgDataProvider);
  return typeface;
}

std::shared_ptr<Typeface> Typeface::MakeFromBytes(const void* bytes, size_t length, int ttcIndex) {
  auto data = Data::MakeWithCopy(bytes, length);
  return MakeFromData(std::move(data), ttcIndex);
}

std::shared_ptr<Typeface> Typeface::MakeFromData(std::shared_ptr<Data> data, int) {
  if (data == nullptr || data->empty()) {
    return nullptr;
  }
  auto cfData =
      CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, static_cast<const UInt8*>(data->data()),
                                  static_cast<CFIndex>(data->size()), kCFAllocatorNull);
  if (cfData == nullptr) {
    return nullptr;
  }
  std::shared_ptr<CGTypeface> typeface;
  auto cfDesc = CTFontManagerCreateFontDescriptorFromData(cfData);
  if (cfDesc) {
    auto ctFont = CTFontCreateWithFontDescriptor(cfDesc, 0, nullptr);
    if (ctFont) {
      typeface = CGTypeface::Make(ctFont, std::move(data));
      CFRelease(ctFont);
    }
    CFRelease(cfDesc);
  }
  CFRelease(cfData);
  return typeface;
}

std::shared_ptr<CGTypeface> CGTypeface::Make(CTFontRef ctFont, std::shared_ptr<Data> data) {
  if (ctFont == nullptr) {
    return nullptr;
  }
  auto typeface = std::shared_ptr<CGTypeface>(new CGTypeface(ctFont, std::move(data)));
  typeface->weakThis = typeface;
  return typeface;
}

static bool HasOutlines(CTFontRef ctFont) {
  auto fontFormat = CTFontCopyAttribute(ctFont, kCTFontFormatAttribute);
  if (!fontFormat) {
    return false;
  }
  SInt16 format;
  CFNumberGetValue(static_cast<CFNumberRef>(fontFormat), kCFNumberSInt16Type, &format);
  CFRelease(fontFormat);
  if (format == kCTFontFormatUnrecognized || format == kCTFontFormatBitmap) {
    return false;
  }
  return true;
}

CGTypeface::CGTypeface(CTFontRef ctFont, std::shared_ptr<Data> data)
    : _uniqueID(UniqueID::Next()), ctFont(ctFont), data(std::move(data)) {
  CFRetain(ctFont);
  CTFontSymbolicTraits traits = CTFontGetSymbolicTraits(ctFont);
  _hasColor = static_cast<bool>(traits & kCTFontTraitColorGlyphs);
  _hasOutlines = HasOutlines(ctFont);
}

CGTypeface::~CGTypeface() {
  if (ctFont) {
    CFRelease(ctFont);
  }
}

std::string CGTypeface::fontFamily() const {
  std::string familyName;
  auto ctFamilyName = CTFontCopyName(ctFont, kCTFontFamilyNameKey);
  if (ctFamilyName != nullptr) {
    familyName = StringFromCFString(ctFamilyName);
    CFRelease(ctFamilyName);
  }
  return familyName;
}

std::string CGTypeface::fontStyle() const {
  std::string styleName;
  auto ctStyleName = CTFontCopyName(ctFont, kCTFontStyleNameKey);
  if (ctStyleName != nullptr) {
    styleName = StringFromCFString(ctStyleName);
    CFRelease(ctStyleName);
  }
  return styleName;
}

int CGTypeface::unitsPerEm() const {
  auto cgFont = CTFontCopyGraphicsFont(ctFont, nullptr);
  auto ret = CGFontGetUnitsPerEm(cgFont);
  CGFontRelease(cgFont);
  return ret;
}

static size_t ToUTF16(int32_t uni, uint16_t utf16[2]) {
  if ((uint32_t)uni > 0x10FFFF) {
    return 0;
  }
  int extra = (uni > 0xFFFF);
  if (utf16) {
    if (extra) {
      utf16[0] = (uint16_t)((0xD800 - 64) + (uni >> 10));
      utf16[1] = (uint16_t)(0xDC00 | (uni & 0x3FF));
    } else {
      utf16[0] = (uint16_t)uni;
    }
  }
  return static_cast<size_t>(1 + extra);
}

GlyphID CGTypeface::getGlyphID(Unichar unichar) const {
  UniChar utf16[2] = {0, 0};
  auto srcCount = ToUTF16(unichar, utf16);
  GlyphID macGlyphs[2] = {0, 0};
  CTFontGetGlyphsForCharacters(ctFont, utf16, macGlyphs, static_cast<CFIndex>(srcCount));
  return macGlyphs[0];
}

std::unique_ptr<Stream> CGTypeface::openStream() const {
  return Stream::MakeFromData(data);
}

std::shared_ptr<Data> CGTypeface::copyTableData(FontTableTag tag) const {
  auto cfData =
      CTFontCopyTable(ctFont, static_cast<CTFontTableTag>(tag), kCTFontTableOptionNoOptions);
  if (cfData == nullptr) {
    auto cgFont = CTFontCopyGraphicsFont(ctFont, nullptr);
    cfData = CGFontCopyTableForTag(cgFont, tag);
    CGFontRelease(cgFont);
  }
  if (cfData == nullptr) {
    return nullptr;
  }
  auto bytePtr = CFDataGetBytePtr(cfData);
  auto length = static_cast<size_t>(CFDataGetLength(cfData));
  return Data::MakeAdopted(
      bytePtr, length, [](const void*, void* context) { CFRelease((CFDataRef)context); },
      (void*)cfData);
}

#ifdef TGFX_USE_GLYPH_TO_UNICODE

static std::vector<Unichar> GetGlyphMapByChar(CTFontRef ctFont, CFIndex glyphCount) {
  std::vector<Unichar> returnMap(static_cast<size_t>(glyphCount), 0);
  UniChar unichar = 0;
  while (glyphCount > 0) {
    CGGlyph glyph;
    if (CTFontGetGlyphsForCharacters(ctFont, &unichar, &glyph, 1)) {
      if (returnMap[glyph] == 0) {
        returnMap[glyph] = unichar;
        --glyphCount;
      }
    }
    if (++unichar == 0) {
      break;
    }
  }
  return returnMap;
}

static constexpr uint16_t PLANE_SIZE = 1 << 13;

static void GetGlyphMapByPlane(const uint8_t* bits, CTFontRef ctFont, std::vector<Unichar>& map,
                               uint8_t planeIndex) {
  Unichar planeOrigin = planeIndex << 16;  // top half of codepoint.
  for (uint16_t i = 0; i < PLANE_SIZE; i++) {
    uint8_t mask = bits[i];
    if (!mask) {
      continue;
    }
    for (uint8_t j = 0; j < 8; j++) {
      if (0 == (mask & (static_cast<uint8_t>(1) << j))) {
        continue;
      }
      auto planeOffset = static_cast<uint16_t>((i << 3) | j);
      Unichar codepoint = planeOrigin | static_cast<Unichar>(planeOffset);
      uint16_t utf16[2] = {planeOffset, 0};
      size_t count = 1;
      if (planeOrigin != 0) {
        count = ToUTF16(codepoint, utf16);
      }
      CGGlyph glyphs[2] = {0, 0};
      if (CTFontGetGlyphsForCharacters(ctFont, utf16, glyphs, static_cast<CFIndex>(count))) {
        if (map[glyphs[0]] < 0x20) {
          map[glyphs[0]] = codepoint;
        }
      }
    }
  }
}

const std::vector<Unichar>& CGTypeface::getGlyphToUnicodeMap() const {
  auto glyphCount = CTFontGetGlyphCount(ctFont);

  auto charSet = CTFontCopyCharacterSet(ctFont);
  if (!charSet) {
    return GetGlyphMapByChar(ctFont, glyphCount);
  }

  auto bitmap = CFCharacterSetCreateBitmapRepresentation(nullptr, charSet);
  if (!bitmap) {
    return {};
  }

  CFIndex dataLength = CFDataGetLength(bitmap);
  if (dataLength == 0) {
    return {};
  }

  std::vector<Unichar> returnMap(static_cast<size_t>(glyphCount), 0);
  auto bits = CFDataGetBytePtr(bitmap);
  GetGlyphMapByPlane(bits, ctFont, returnMap, 0);
  /*
    A CFData object that specifies the bitmap representation of the Unicode
    character points the for the new character set. The bitmap representation could
    contain all the Unicode character range starting from BMP to Plane 16. The
    first 8KiB (8192 bytes) of the data represent the BMP range. The BMP range 8KiB
    can be followed by zero to sixteen 8KiB bitmaps, each prepended with the plane
    index byte. For example, the bitmap representing the BMP and Plane 2 has the
    size of 16385 bytes (8KiB for BMP, 1 byte index, and a 8KiB bitmap for Plane
    2). The plane index byte, in this case, contains the integer value two.
    */

  if (dataLength <= PLANE_SIZE) {
    return returnMap;
  }
  auto extraPlaneCount = (dataLength - PLANE_SIZE) / (1 + PLANE_SIZE);
  while (extraPlaneCount-- > 0) {
    bits += PLANE_SIZE;
    uint8_t planeIndex = *bits++;
    GetGlyphMapByPlane(bits, ctFont, returnMap, planeIndex);
  }
  return returnMap;
}
#endif

#ifdef TGFX_USE_ADVANCED_TYPEFACE_PROPERTY
AdvancedTypefaceInfo CGTypeface::getAdvancedInfo() const {
  AdvancedTypefaceInfo advancedProperty;
  auto fontName = CTFontCopyPostScriptName(ctFont);
  if (fontName) {
    advancedProperty.postScriptName = CGTypeface::StringFromCFString(fontName);
  }
  CFRelease(fontName);

  constexpr auto glyf = SetFourByteTag('g', 'l', 'y', 'f');
  constexpr auto loca = SetFourByteTag('l', 'o', 'c', 'a');
  constexpr auto CFF = SetFourByteTag('C', 'F', 'F', ' ');
  // Use copyTableData to check if the font table exists.
  // TODO(YGaurora): Implement a function to check for the existence of a font table without
  // copying its data, which would improve performance.
  if (copyTableData(glyf) && copyTableData(loca)) {
    advancedProperty.type = AdvancedTypefaceInfo::FontType::TrueType;
  } else if (copyTableData(CFF)) {
    advancedProperty.type = AdvancedTypefaceInfo::FontType::CFF;
  }

  CTFontSymbolicTraits symbolicTraits = CTFontGetSymbolicTraits(ctFont);
  if (symbolicTraits & kCTFontMonoSpaceTrait) {
    advancedProperty.style = static_cast<AdvancedTypefaceInfo::StyleFlags>(
        advancedProperty.style | AdvancedTypefaceInfo::StyleFlags::FixedPitch);
  }
  if (symbolicTraits & kCTFontItalicTrait) {
    advancedProperty.style = static_cast<AdvancedTypefaceInfo::StyleFlags>(
        advancedProperty.style | AdvancedTypefaceInfo::StyleFlags::Italic);
  }
  CTFontStylisticClass stylisticClass = symbolicTraits & kCTFontClassMaskTrait;
  if (stylisticClass >= kCTFontOldStyleSerifsClass && stylisticClass <= kCTFontSlabSerifsClass) {
    advancedProperty.style = static_cast<AdvancedTypefaceInfo::StyleFlags>(
        advancedProperty.style | AdvancedTypefaceInfo::StyleFlags::Serif);
  } else if (stylisticClass & kCTFontSymbolicClass) {
    advancedProperty.style = static_cast<AdvancedTypefaceInfo::StyleFlags>(
        advancedProperty.style | AdvancedTypefaceInfo::StyleFlags::Symbolic);
  }
  return advancedProperty;
}
#endif

std::shared_ptr<ScalerContext> CGTypeface::onCreateScalerContext(float size) const {
  return std::make_shared<CGScalerContext>(weakThis.lock(), size);
}
}  // namespace tgfx
