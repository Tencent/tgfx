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

#include "tgfx/core/ColorSpace.h"
#include <skcms.h>
#include <cmath>
#include <sstream>
#include <vector>
#include "../../include/tgfx/core/Log.h"
#include "Checksum.h"

namespace tgfx {

#if !defined(TGFX_CPU_BIG_ENDIAN) && !defined(TGFX_CPU_LITTLE_ENDIAN)
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define TGFX_CPU_BIG_ENDIAN
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define TGFX_CPU_LITTLE_ENDIAN
#elif defined(__sparc) || defined(__sparc__) || defined(_POWER) || defined(__powerpc__) || \
    defined(__ppc__) || defined(__hppa) || defined(__PPC__) || defined(__PPC64__) ||       \
    defined(_MIPSEB) || defined(__ARMEB__) || defined(__s390__) ||                         \
    (defined(__sh__) && defined(__BIG_ENDIAN__)) || (defined(__ia64) && defined(__BIG_ENDIAN__))
#define TGFX_CPU_BIG_ENDIAN
#else
#define TGFX_CPU_LITTLE_ENDIAN
#endif
#endif

static uint16_t EndianSwap16(uint16_t value) {
  return static_cast<uint16_t>((value >> 8) | ((value & 0xFF) << 8));
}

static uint32_t EndianSwap32(uint32_t value) {
  return ((value & 0xFF) << 24) | ((value & 0xFF00) << 8) | ((value & 0xFF0000) >> 8) |
         (value >> 24);
}

#ifdef TGFX_CPU_LITTLE_ENDIAN
#define SWAP_UINT16_TO_BIG_ENDIAN(n) EndianSwap16(n)
#define SWAP_UINT32_TO_BIG_ENDIAN(n) EndianSwap32(n)
#else
#define SWAP_UINT16_TO_BIG_ENDIAN(n) static_cast<uint16_t>(n)
#define SWAP_UINT32_TO_BIG_ENDIAN(n) static_cast<uint32_t>(n)
#endif

static bool GetPrimariesByID(ColorSpacePrimariesID primaries,
                             ColorSpacePrimaries* colorSpacePrimaries) {
  // Rec. ITU-T H.273, Table 2.
  switch (primaries) {
    case ColorSpacePrimariesID::Rec709:
      *colorSpacePrimaries = NamedPrimaries::Rec709;
      return true;
    case ColorSpacePrimariesID::Rec470SystemM:
      *colorSpacePrimaries = NamedPrimaries::Rec470SystemM;
      return true;
    case ColorSpacePrimariesID::Rec470SystemBG:
      *colorSpacePrimaries = NamedPrimaries::Rec470SystemBG;
      return true;
    case ColorSpacePrimariesID::Rec601:
      *colorSpacePrimaries = NamedPrimaries::Rec601;
      return true;
    case ColorSpacePrimariesID::SMPTE_ST_240:
      *colorSpacePrimaries = NamedPrimaries::SMPTE_ST_240;
      return true;
    case ColorSpacePrimariesID::GenericFilm:
      *colorSpacePrimaries = NamedPrimaries::GenericFilm;
      return true;
    case ColorSpacePrimariesID::Rec2020:
      *colorSpacePrimaries = NamedPrimaries::Rec2020;
      return true;
    case ColorSpacePrimariesID::SMPTE_ST_428_1:
      *colorSpacePrimaries = NamedPrimaries::SMPTE_ST_428_1;
      return true;
    case ColorSpacePrimariesID::SMPTE_RP_431_2:
      *colorSpacePrimaries = NamedPrimaries::SMPTE_RP_431_2;
      return true;
    case ColorSpacePrimariesID::SMPTE_EG_432_1:
      *colorSpacePrimaries = NamedPrimaries::SMPTE_EG_432_1;
      return true;
    case ColorSpacePrimariesID::ITU_T_H273_Value22:
      *colorSpacePrimaries = NamedPrimaries::ITU_T_H273_Value22;
      return true;
    default:
      // Reserved or unimplemented.
      break;
  }
  return false;
}

static bool GetTransferFunctionByID(TransferFunctionID transferCharacteristics,
                                    TransferFunction* transferFunction) {
  // Rec. ITU-T H.273, Table 3.
  switch (transferCharacteristics) {
    case TransferFunctionID::Rec709:
      *transferFunction = NamedTransferFunction::Rec709;
      return true;
    case TransferFunctionID::Rec470SystemM:
      *transferFunction = NamedTransferFunction::Rec470SystemM;
      return true;
    case TransferFunctionID::Rec470SystemBG:
      *transferFunction = NamedTransferFunction::Rec470SystemBG;
      return true;
    case TransferFunctionID::Rec601:
      *transferFunction = NamedTransferFunction::Rec601;
      return true;
    case TransferFunctionID::SMPTE_ST_240:
      *transferFunction = NamedTransferFunction::SMPTE_ST_240;
      return true;
    case TransferFunctionID::Linear:
      *transferFunction = NamedTransferFunction::Linear;
      return true;
    case TransferFunctionID::IEC61966_2_4:
      *transferFunction = NamedTransferFunction::IEC61966_2_4;
      return true;
    case TransferFunctionID::IEC61966_2_1:
      *transferFunction = NamedTransferFunction::IEC61966_2_1;
      return true;
    case TransferFunctionID::Rec2020_10bit:
      *transferFunction = NamedTransferFunction::Rec2020_10bit;
      return true;
    case TransferFunctionID::Rec2020_12bit:
      *transferFunction = NamedTransferFunction::Rec2020_12bit;
      return true;
    case TransferFunctionID::SMPTE_ST_428_1:
      *transferFunction = NamedTransferFunction::SMPTE_ST_428_1;
      return true;
    default:
      // Reserved or unimplemented.
      break;
  }
  return false;
}

static bool ColorSpaceAlmostEqual(float a, float b) {
  return fabsf(a - b) < 0.01f;
}

static bool XYZAlmostEqual(const ColorMatrix33& mA, const ColorMatrix33& mB) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      if (!ColorSpaceAlmostEqual(mA.values[r][c], mB.values[r][c])) {
        return false;
      }
    }
  }
  return true;
}

/**
 * Let's use a stricter version for transfer functions.  Worst case, these are encoded in ICC
 * format, which offers 16-bits of fractional precision.
 */
static bool TransferFnAlmostEqual(float a, float b) {
  return fabsf(a - b) < 0.001f;
}

static bool IsAlmostSRGB(const TransferFunction& coeffs) {
  return TransferFnAlmostEqual(NamedTransferFunction::SRGB.a, coeffs.a) &&
         TransferFnAlmostEqual(NamedTransferFunction::SRGB.b, coeffs.b) &&
         TransferFnAlmostEqual(NamedTransferFunction::SRGB.c, coeffs.c) &&
         TransferFnAlmostEqual(NamedTransferFunction::SRGB.d, coeffs.d) &&
         TransferFnAlmostEqual(NamedTransferFunction::SRGB.e, coeffs.e) &&
         TransferFnAlmostEqual(NamedTransferFunction::SRGB.f, coeffs.f) &&
         TransferFnAlmostEqual(NamedTransferFunction::SRGB.g, coeffs.g);
}

static bool IsAlmost2dot2(const TransferFunction& coeffs) {
  return TransferFnAlmostEqual(1.0f, coeffs.a) && TransferFnAlmostEqual(0.0f, coeffs.b) &&
         TransferFnAlmostEqual(0.0f, coeffs.e) && TransferFnAlmostEqual(2.2f, coeffs.g) &&
         coeffs.d <= 0.0f;
}

static bool IsAlmostLinear(const TransferFunction& coeffs) {
  // OutputVal = InputVal ^ 1.0f
  const bool linearExp = TransferFnAlmostEqual(1.0f, coeffs.a) &&
                         TransferFnAlmostEqual(0.0f, coeffs.b) &&
                         TransferFnAlmostEqual(0.0f, coeffs.e) &&
                         TransferFnAlmostEqual(1.0f, coeffs.g) && coeffs.d <= 0.0f;

  // OutputVal = 1.0f * InputVal
  const bool linearFn = TransferFnAlmostEqual(1.0f, coeffs.c) &&
                        TransferFnAlmostEqual(0.0f, coeffs.f) && coeffs.d >= 1.0f;

  return linearExp || linearFn;
}

static bool NearlyEqual(float x, float y) {
  /**
   * A note on why I chose this tolerance:  TransferFnAlmostEqual() uses a tolerance of 0.001f,
   * which doesn't seem to be enough to distinguish between similar transfer functions, for example:
   * gamma2.2 and sRGB.
   *
   * If the tolerance is 0.0f, then this we can't distinguish between two different encodings of
   * what is clearly the same colorspace. Some experimentation with example files lead to this
   * number:
   */
  static constexpr float Tolerance = 1.0f / (1 << 11);
  return ::fabsf(x - y) <= Tolerance;
}

static bool NearlyEqual(const gfx::skcms_TransferFunction& u,
                        const gfx::skcms_TransferFunction& v) {
  return NearlyEqual(u.g, v.g) && NearlyEqual(u.a, v.a) && NearlyEqual(u.b, v.b) &&
         NearlyEqual(u.c, v.c) && NearlyEqual(u.d, v.d) && NearlyEqual(u.e, v.e) &&
         NearlyEqual(u.f, v.f);
}

static bool NearlyEqual(const gfx::skcms_Matrix3x3& u, const gfx::skcms_Matrix3x3& v) {
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      if (!NearlyEqual(u.vals[r][c], v.vals[r][c])) {
        return false;
      }
    }
  }
  return true;
}

constexpr uint32_t CICPTranferFunctionSRGB = 1;
constexpr uint32_t CICPTranferFunction2Dot2 = 4;
constexpr uint32_t CICPTranferFunctionLinear = 8;

static uint32_t GetCICPTranferFunction(const gfx::skcms_TransferFunction& function) {
  if (gfx::skcms_TransferFunction_getType(&function) == gfx::skcms_TFType_sRGBish) {
    if (NearlyEqual(function, *reinterpret_cast<const gfx::skcms_TransferFunction*>(
                                  &NamedTransferFunction::SRGB))) {
      return CICPTranferFunctionSRGB;
    }
    if (NearlyEqual(function, *reinterpret_cast<const gfx::skcms_TransferFunction*>(
                                  &NamedTransferFunction::TwoDotTwo))) {
      return CICPTranferFunction2Dot2;
    }
    if (NearlyEqual(function, *reinterpret_cast<const gfx::skcms_TransferFunction*>(
                                  &NamedTransferFunction::Linear))) {
      return CICPTranferFunctionLinear;
    }
  }
  return 0;
}

constexpr uint32_t CICPPrimariesSRGB = 1;
constexpr uint32_t CICPPrimariesP3 = 12;
constexpr uint32_t CICPPrimariesRec2020 = 9;

static uint32_t GetCICPPrimaries(const gfx::skcms_Matrix3x3& toXYZD50) {
  if (NearlyEqual(toXYZD50, *reinterpret_cast<const gfx::skcms_Matrix3x3*>(&NamedGamut::SRGB))) {
    return CICPPrimariesSRGB;
  }
  if (NearlyEqual(toXYZD50,
                  *reinterpret_cast<const gfx::skcms_Matrix3x3*>(&NamedGamut::DisplayP3))) {
    return CICPPrimariesP3;
  }
  if (NearlyEqual(toXYZD50, *reinterpret_cast<const gfx::skcms_Matrix3x3*>(&NamedGamut::Rec2020))) {
    return CICPPrimariesRec2020;
  }
  return 0;
}

static std::string GetDescString(const gfx::skcms_TransferFunction& function,
                                 const gfx::skcms_Matrix3x3& toXYZD50, uint64_t hash) {
  const uint32_t cicpTrfn = GetCICPTranferFunction(function);
  const uint32_t cicpPrimaries = GetCICPPrimaries(toXYZD50);

  // Use a unique string for sRGB.
  if (cicpTrfn == CICPTranferFunctionSRGB && cicpPrimaries == CICPPrimariesSRGB) {
    return "sRGB";
  }

  // If available, use the named CICP primaries and transfer function.
  if (cicpPrimaries && cicpTrfn) {
    std::string result;
    switch (cicpPrimaries) {
      case CICPPrimariesSRGB:
        result += "sRGB";
        break;
      case CICPPrimariesP3:
        result += "Display P3";
        break;
      case CICPPrimariesRec2020:
        result += "Rec2020";
        break;
      default:
        result += "Unknown";
        break;
    }
    result += " Gamut with ";
    switch (cicpTrfn) {
      case CICPTranferFunctionSRGB:
        result += "sRGB";
        break;
      case CICPTranferFunctionLinear:
        result += "Linear";
        break;
      case CICPTranferFunction2Dot2:
        result += "2.2";
        break;
      default:
        result += "Unknown";
        break;
    }
    result += " Transfer";
    return result;
  }

  // Fall back to a prefix plus hash.
  return std::string("Tencent/TGFX/") + std::to_string(hash);
}

static constexpr uint32_t SetFourByteTag(char a, char b, char c, char d) {
  return (((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d);
}

/**
 * This is equal to the header size according to the ICC specification (128) plus the size of the
 * tag count (4).  We include the tag count since we always require it to be present anyway.
 */
static constexpr size_t ICCHeaderSize = 132;

/**
 * Contains a signature (4), offset (4), and size (4).
 */
static constexpr size_t ICCTagTableEntrySize = 12;

static constexpr uint32_t DisplayProfile = SetFourByteTag('m', 'n', 't', 'r');
static constexpr uint32_t RGBColorSpace = SetFourByteTag('R', 'G', 'B', ' ');
static constexpr uint32_t XYZPCSSpace = SetFourByteTag('X', 'Y', 'Z', ' ');
static constexpr uint32_t ACSPSignature = SetFourByteTag('a', 'c', 's', 'p');

static constexpr uint32_t TagRXYZ = SetFourByteTag('r', 'X', 'Y', 'Z');
static constexpr uint32_t TagGXYZ = SetFourByteTag('g', 'X', 'Y', 'Z');
static constexpr uint32_t TagBXYZ = SetFourByteTag('b', 'X', 'Y', 'Z');
static constexpr uint32_t TagWTPT = SetFourByteTag('w', 't', 'p', 't');
static constexpr uint32_t TagRTRC = SetFourByteTag('r', 'T', 'R', 'C');
static constexpr uint32_t TagGTRC = SetFourByteTag('g', 'T', 'R', 'C');
static constexpr uint32_t TagBTRC = SetFourByteTag('b', 'T', 'R', 'C');
static constexpr uint32_t TagCPRT = SetFourByteTag('c', 'p', 'r', 't');
static constexpr uint32_t TagDESC = SetFourByteTag('d', 'e', 's', 'c');

static constexpr uint32_t TagCurveType = SetFourByteTag('c', 'u', 'r', 'v');
static constexpr uint32_t TagParaCurveType = SetFourByteTag('p', 'a', 'r', 'a');
static constexpr uint32_t TagTextType = SetFourByteTag('m', 'l', 'u', 'c');

enum ParaCurveType {
  ExponentialParaCurveType = 0,
  GABParaCurveType = 1,
  GABCParaCurveType = 2,
  GABDEParaCurveType = 3,
  GABCDEFParaCurveType = 4,
};

/**
 * The D50 illuminant.
 */
static constexpr float kD50_x = 0.9642f;
static constexpr float kD50_y = 1.0000f;
static constexpr float kD50_z = 0.8249f;

static constexpr int MaxS32FitsInFloat = 2147483520;
static constexpr int MinS32FitsInFloat = -MaxS32FitsInFloat;

static constexpr int FloatSaturate2Int(float x) {
  x = x < MaxS32FitsInFloat ? x : MaxS32FitsInFloat;
  x = x > MinS32FitsInFloat ? x : MinS32FitsInFloat;
  return (int)x;
}

static int32_t FloatRoundToFixed(float x) {
  return FloatSaturate2Int((float)floor((double)x * (1 << 16) + 0.5));
}

struct ICCHeader {
  // Size of the profile (computed)
  uint32_t size;

  // Preferred CMM type (ignored)
  uint32_t cmm_type = 0;

  // Version 4.3 or 4.4 if CICP is included.
  uint32_t version = SWAP_UINT32_TO_BIG_ENDIAN(0x04300000);

  // Display device profile
  uint32_t profile_class = SWAP_UINT32_TO_BIG_ENDIAN(DisplayProfile);

  // RGB input color space;
  uint32_t data_color_space = SWAP_UINT32_TO_BIG_ENDIAN(RGBColorSpace);

  // Profile connection space.
  uint32_t pcs = SWAP_UINT32_TO_BIG_ENDIAN(XYZPCSSpace);

  // Date and time (ignored)
  uint16_t creation_date_year = SWAP_UINT16_TO_BIG_ENDIAN(2025);
  uint16_t creation_date_month = SWAP_UINT16_TO_BIG_ENDIAN(9);  // 1-12
  uint16_t creation_date_day = SWAP_UINT16_TO_BIG_ENDIAN(23);   // 1-31
  uint16_t creation_date_hours = 0;                             // 0-23
  uint16_t creation_date_minutes = 0;                           // 0-59
  uint16_t creation_date_seconds = 0;                           // 0-59

  // Profile signature
  uint32_t signature = SWAP_UINT32_TO_BIG_ENDIAN(ACSPSignature);

  // Platform target (ignored)
  uint32_t platform = 0;

  // Flags: not embedded, can be used independently
  uint32_t flags = 0x00000000;

  // Device manufacturer (ignored)
  uint32_t device_manufacturer = 0;

  // Device model (ignored)
  uint32_t device_model = 0;

  // Device attributes (ignored)
  uint8_t device_attributes[8] = {0};

  // Relative colorimetric rendering intent
  uint32_t rendering_intent = SWAP_UINT32_TO_BIG_ENDIAN(1);

  // D50 standard illuminant (X, Y, Z)
  uint32_t illuminant_X =
      SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(FloatRoundToFixed(kD50_x)));
  uint32_t illuminant_Y =
      SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(FloatRoundToFixed(kD50_y)));
  uint32_t illuminant_Z =
      SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(FloatRoundToFixed(kD50_z)));

  // Profile creator (ignored)
  uint32_t creator = 0;

  // Profile id checksum (ignored)
  uint8_t profile_id[16] = {0};

  // Reserved (ignored)
  uint8_t reserved[28] = {0};

  // Technically not part of header, but required
  uint32_t tag_count = 0;
};

static std::shared_ptr<Data> WriteXYZTag(float x, float y, float z) {
  uint32_t data[] = {
      SWAP_UINT32_TO_BIG_ENDIAN(XYZPCSSpace),
      0,
      SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(FloatRoundToFixed(x))),
      SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(FloatRoundToFixed(y))),
      SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(FloatRoundToFixed(z))),
  };
  return Data::MakeWithCopy(data, sizeof(data));
}

static void StreamWriteU16BE(std::ostringstream* s, uint16_t value) {
  value = SWAP_UINT16_TO_BIG_ENDIAN(value);
  s->write((char*)&value, sizeof(value));
}

static void StreamWriteU32BE(std::ostringstream* s, uint32_t value) {
  value = EndianSwap32(value);
  s->write((char*)&value, sizeof(value));
}

// Write a lookup table based 1D curve.
static std::shared_ptr<Data> WriteTrcTag(const gfx::skcms_Curve& trc) {
  std::ostringstream s{std::ios::binary};
  if (trc.table_entries) {
    StreamWriteU32BE(&s, TagCurveType);       // Type
    StreamWriteU32BE(&s, 0);                  // Reserved
    StreamWriteU32BE(&s, trc.table_entries);  // Value count
    for (uint32_t i = 0; i < trc.table_entries; ++i) {
      uint16_t value = reinterpret_cast<const uint16_t*>(trc.table_16)[i];
      s.write((char*)&value, sizeof(value));
    }
  } else {
    StreamWriteU32BE(&s, TagParaCurveType);  // Type
    StreamWriteU32BE(&s, 0);                 // Reserved
    const auto& fn = trc.parametric;
    DEBUG_ASSERT(skcms_TransferFunction_isSRGBish(&fn));
    if (fn.a == 1.f && fn.b == 0.f && fn.c == 0.f && fn.d == 0.f && fn.e == 0.f && fn.f == 0.f) {
      StreamWriteU16BE(&s, ExponentialParaCurveType);
      StreamWriteU16BE(&s, 0);
      StreamWriteU32BE(&s, static_cast<uint32_t>(FloatRoundToFixed(fn.g)));
    } else {
      StreamWriteU16BE(&s, GABCDEFParaCurveType);
      StreamWriteU16BE(&s, 0);
      StreamWriteU32BE(&s, static_cast<uint32_t>(FloatRoundToFixed(fn.g)));
      StreamWriteU32BE(&s, static_cast<uint32_t>(FloatRoundToFixed(fn.a)));
      StreamWriteU32BE(&s, static_cast<uint32_t>(FloatRoundToFixed(fn.b)));
      StreamWriteU32BE(&s, static_cast<uint32_t>(FloatRoundToFixed(fn.c)));
      StreamWriteU32BE(&s, static_cast<uint32_t>(FloatRoundToFixed(fn.d)));
      StreamWriteU32BE(&s, static_cast<uint32_t>(FloatRoundToFixed(fn.e)));
      StreamWriteU32BE(&s, static_cast<uint32_t>(FloatRoundToFixed(fn.f)));
    }
  }
  std::streamsize paddingBytes = (4 - (s.tellp() % 4)) % 4;
  int padding = 0;
  s.write((char*)&padding, paddingBytes);
  std::string stingData = s.str();
  return Data::MakeWithCopy(stingData.data(), stingData.size());
}

std::shared_ptr<Data> WriteTextTag(const char* text) {
  uint32_t text_length = static_cast<uint32_t>(strlen(text));
  uint32_t header[] = {
      SWAP_UINT32_TO_BIG_ENDIAN(TagTextType),                         // Type signature
      0,                                                              // Reserved
      SWAP_UINT32_TO_BIG_ENDIAN(1),                                   // Number of records
      SWAP_UINT32_TO_BIG_ENDIAN(12),                                  // Record size (must be 12)
      SWAP_UINT32_TO_BIG_ENDIAN(SetFourByteTag('e', 'n', 'U', 'S')),  // English USA
      SWAP_UINT32_TO_BIG_ENDIAN(2 * text_length),                     // Length of string in bytes
      SWAP_UINT32_TO_BIG_ENDIAN(28),                                  // Offset of string
  };
  std::ostringstream s{std::ios::binary};
  s.write((char*)header, sizeof(header));
  for (size_t i = 0; i < text_length; i++) {
    // Convert ASCII to big-endian UTF-16.
    uint8_t zero = 0;
    s.write((char*)&zero, sizeof(zero));
    s.write(&text[i], sizeof(text[i]));
  }
  std::streamsize paddingBytes = (4 - (s.tellp() % 4)) % 4;
  int padding = 0;
  s.write((char*)&padding, paddingBytes);
  std::string stingData = s.str();
  return Data::MakeWithCopy(stingData.data(), stingData.size());
}

static std::shared_ptr<Data> WriteICCProfile(const gfx::skcms_ICCProfile* profile,
                                             const char* desc) {
  ICCHeader header;
  std::vector<std::pair<uint32_t, std::shared_ptr<Data>>> tags;

  // Compute primaries.
  if (profile->has_toXYZD50) {
    const auto& m = profile->toXYZD50;
    tags.emplace_back(TagRXYZ, WriteXYZTag(m.vals[0][0], m.vals[1][0], m.vals[2][0]));
    tags.emplace_back(TagGXYZ, WriteXYZTag(m.vals[0][1], m.vals[1][1], m.vals[2][1]));
    tags.emplace_back(TagBXYZ, WriteXYZTag(m.vals[0][2], m.vals[1][2], m.vals[2][2]));
  }
  // Compute white point tag (must be D50)
  tags.emplace_back(TagWTPT, WriteXYZTag(kD50_x, kD50_y, kD50_z));

  // Compute transfer curves.
  if (profile->has_trc) {
    tags.emplace_back(TagRTRC, WriteTrcTag(profile->trc[0]));

    // Use empty data to indicate that the entry should use the previous tag's
    // data.
    if (!memcmp(&profile->trc[1], &profile->trc[0], sizeof(profile->trc[0]))) {
      tags.emplace_back(TagGTRC, Data::MakeEmpty());
    } else {
      tags.emplace_back(TagGTRC, WriteTrcTag(profile->trc[1]));
    }

    if (!memcmp(&profile->trc[2], &profile->trc[1], sizeof(profile->trc[1]))) {
      tags.emplace_back(TagBTRC, Data::MakeEmpty());
    } else {
      tags.emplace_back(TagBTRC, WriteTrcTag(profile->trc[2]));
    }
  }

  // Compute copyright tag
  tags.emplace_back(TagCPRT, WriteTextTag("Tencent Inc. 2025"));

  if (desc && *desc != '\0') {
    // Compute profile description tag
    tags.emplace(tags.begin(), TagDESC, WriteTextTag(desc));
  }

  // Compute the size of the profile.
  size_t tagDataSize = 0;
  for (const auto& tag : tags) {
    tagDataSize += tag.second->size();
  }
  size_t tagTableSize = ICCTagTableEntrySize * tags.size();
  size_t profileSize = ICCHeaderSize + tagTableSize + tagDataSize;

  // Write the header.
  header.data_color_space = SWAP_UINT32_TO_BIG_ENDIAN(profile->data_color_space);
  header.pcs = SWAP_UINT32_TO_BIG_ENDIAN(profile->pcs);
  header.size = SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(profileSize));
  header.tag_count = SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(tags.size()));
  auto ptr = (uint8_t*)malloc(profileSize);
  auto tempPtr = ptr;
  memcpy(tempPtr, &header, sizeof(header));
  tempPtr += sizeof(header);

  // Write the tag table. Track the offset and size of the previous tag to
  // compute each tag's offset. An empty Data indicates that the previous
  // tag is to be reused.
  size_t lastTagOffset = sizeof(header) + tagTableSize;
  size_t lastTagSize = 0;
  for (const auto& tag : tags) {
    if (!tag.second->empty()) {
      lastTagOffset = lastTagOffset + lastTagSize;
      lastTagSize = tag.second->size();
    }
    uint32_t tagTableEntry[3] = {
        SWAP_UINT32_TO_BIG_ENDIAN(tag.first),
        SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(lastTagOffset)),
        SWAP_UINT32_TO_BIG_ENDIAN(static_cast<uint32_t>(lastTagSize)),
    };
    memcpy(tempPtr, tagTableEntry, sizeof(tagTableEntry));
    tempPtr += sizeof(tagTableEntry);
  }

  // Write the tags.
  for (const auto& tag : tags) {
    if (tag.second->empty()) continue;
    memcpy(tempPtr, tag.second->data(), tag.second->size());
    tempPtr += tag.second->size();
  }
  DEBUG_ASSERT(profileSize == static_cast<size_t>(tempPtr - ptr));
  return Data::MakeAdopted(ptr, profileSize, Data::FreeProc);
}

bool ColorSpacePrimaries::toXYZD50(ColorMatrix33* toXYZD50) const {
  return gfx::skcms_PrimariesToXYZD50(rx, ry, gx, gy, bx, by, wx, wy,
                                      reinterpret_cast<gfx::skcms_Matrix3x3*>(toXYZD50));
}

std::shared_ptr<ColorSpace> ColorSpace::SRGB() {
  static std::shared_ptr<ColorSpace> cs =
      std::shared_ptr<ColorSpace>(new ColorSpace(NamedTransferFunction::SRGB, NamedGamut::SRGB));
  return cs;
}

std::shared_ptr<ColorSpace> ColorSpace::SRGBLinear() {
  static std::shared_ptr<ColorSpace> cs =
      std::shared_ptr<ColorSpace>(new ColorSpace(NamedTransferFunction::Linear, NamedGamut::SRGB));
  return cs;
}

std::shared_ptr<ColorSpace> ColorSpace::MakeRGB(const TransferFunction& transferFunction,
                                                const ColorMatrix33& toXYZ) {
  if (gfx::skcms_TransferFunction_getType(reinterpret_cast<const gfx::skcms_TransferFunction*>(
          &transferFunction)) == gfx::skcms_TFType_Invalid) {
    return nullptr;
  }

  const TransferFunction* tf = &transferFunction;

  if (IsAlmostSRGB(transferFunction)) {
    if (XYZAlmostEqual(toXYZ, NamedGamut::SRGB)) {
      return ColorSpace::SRGB();
    }
    tf = &NamedTransferFunction::SRGB;
  } else if (IsAlmost2dot2(transferFunction)) {
    tf = &NamedTransferFunction::TwoDotTwo;
  } else if (IsAlmostLinear(transferFunction)) {
    if (XYZAlmostEqual(toXYZ, NamedGamut::SRGB)) {
      return ColorSpace::SRGBLinear();
    }
    tf = &NamedTransferFunction::Linear;
  }

  return std::shared_ptr<ColorSpace>(new ColorSpace(*tf, toXYZ));
}

std::shared_ptr<ColorSpace> ColorSpace::MakeCICP(ColorSpacePrimariesID colorPrimaries,
                                                 TransferFunctionID transferCharacteristics) {
  TransferFunction trfn;
  if (!GetTransferFunctionByID(transferCharacteristics, &trfn)) {
    return nullptr;
  }

  ColorSpacePrimaries primaries;
  if (!GetPrimariesByID(colorPrimaries, &primaries)) {
    return nullptr;
  }

  ColorMatrix33 primariesMatrix;
  if (!primaries.toXYZD50(&primariesMatrix)) {
    return nullptr;
  }

  return ColorSpace::MakeRGB(trfn, primariesMatrix);
}

std::shared_ptr<ColorSpace> ColorSpace::MakeFromICC(const void* data, size_t size) {
  gfx::skcms_ICCProfile profile;
  if (!gfx::skcms_Parse(data, size, &profile)) {
    return nullptr;
  }
  if (!profile.has_toXYZD50 || !profile.has_trc) {
    return nullptr;
  }
  if (skcms_ApproximatelyEqualProfiles(&profile, gfx::skcms_sRGB_profile())) {
    return ColorSpace::SRGB();
  }

  gfx::skcms_Matrix3x3 inv;
  if (!skcms_Matrix3x3_invert(&profile.toXYZD50, &inv)) {
    return nullptr;
  }

  const gfx::skcms_Curve* trc = profile.trc;

  if (trc[0].table_entries != 0 || trc[1].table_entries != 0 || trc[2].table_entries != 0 ||
      0 != memcmp(&trc[0].parametric, &trc[1].parametric, sizeof(trc[0].parametric)) ||
      0 != memcmp(&trc[0].parametric, &trc[2].parametric, sizeof(trc[0].parametric))) {
    if (gfx::skcms_TRCs_AreApproximateInverse(
            reinterpret_cast<const gfx::skcms_ICCProfile*>(&profile),
            gfx::skcms_sRGB_Inverse_TransferFunction())) {
      return ColorSpace::MakeRGB(NamedTransferFunction::SRGB,
                                 *reinterpret_cast<ColorMatrix33*>(&profile.toXYZD50));
    }
    return nullptr;
  }

  return ColorSpace::MakeRGB(*reinterpret_cast<TransferFunction*>(&profile.trc[0].parametric),
                             *reinterpret_cast<ColorMatrix33*>(&profile.toXYZD50));
}

bool ColorSpace::gammaCloseToSRGB() const {
  // Nearly-equal transfer functions were snapped at construction time, so just do an exact test
  return memcmp(&_transferFunction, &NamedTransferFunction::SRGB, 7 * sizeof(float)) == 0;
}

bool ColorSpace::gammaIsLinear() const {
  // Nearly-equal transfer functions were snapped at construction time, so just do an exact test
  return memcmp(&_transferFunction, &NamedTransferFunction::Linear, 7 * sizeof(float)) == 0;
}

bool ColorSpace::isNumericalTransferFunction(TransferFunction* function) const {
  *function = this->transferFunction();
  return gfx::skcms_TransferFunction_getType(
             reinterpret_cast<gfx::skcms_TransferFunction*>(function)) == gfx::skcms_TFType_sRGBish;
}

bool ColorSpace::toXYZD50(ColorMatrix33* toXYZD50) const {
  *toXYZD50 = _toXYZD50;
  return true;
}

std::shared_ptr<ColorSpace> ColorSpace::makeLinearGamma() const {
  if (this->gammaIsLinear()) {
    return const_cast<ColorSpace*>(this)->shared_from_this();
  }
  return ColorSpace::MakeRGB(NamedTransferFunction::Linear, _toXYZD50);
}

std::shared_ptr<ColorSpace> ColorSpace::makeSRGBGamma() const {
  if (this->gammaCloseToSRGB()) {
    return const_cast<ColorSpace*>(this)->shared_from_this();
  }
  return ColorSpace::MakeRGB(NamedTransferFunction::SRGB, _toXYZD50);
}

std::shared_ptr<ColorSpace> ColorSpace::makeColorSpin() const {
  gfx::skcms_Matrix3x3 spin = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}};
  gfx::skcms_Matrix3x3 spun =
      gfx::skcms_Matrix3x3_concat(reinterpret_cast<const gfx::skcms_Matrix3x3*>(&_toXYZD50), &spin);
  return std::shared_ptr<ColorSpace>(
      new ColorSpace(_transferFunction, *reinterpret_cast<ColorMatrix33*>(&spun)));
}

bool ColorSpace::isSRGB() const {
  return this == SRGB().get();
}

std::shared_ptr<Data> ColorSpace::toICCProfile() const {
  gfx::skcms_ICCProfile profile;
  memset(&profile, 0, sizeof(profile));
  std::vector<uint16_t> trcTable;
  std::vector<uint16_t> a2bGrid;

  profile.data_color_space = gfx::skcms_Signature_RGB;
  profile.pcs = gfx::skcms_Signature_XYZ;

  auto skcmsToXYZD50 = reinterpret_cast<const gfx::skcms_Matrix3x3*>(&_toXYZD50);
  // Populate toXYZD50.
  {
    profile.has_toXYZD50 = true;
    profile.toXYZD50 = *skcmsToXYZD50;
  }

  auto skcmsTF = reinterpret_cast<const gfx::skcms_TransferFunction*>(&_transferFunction);
  // Populate the analytic TRC for sRGB-like curves.
  if (gfx::skcms_TransferFunction_isSRGBish(skcmsTF)) {
    profile.has_trc = true;
    profile.trc[0].table_entries = 0;
    profile.trc[0].parametric = *skcmsTF;
    memcpy(&profile.trc[1], &profile.trc[0], sizeof(profile.trc[0]));
    memcpy(&profile.trc[2], &profile.trc[0], sizeof(profile.trc[0]));
  }
  std::string description = GetDescString(*skcmsTF, *skcmsToXYZD50, hash());
  return WriteICCProfile(&profile, description.c_str());
}

bool ColorSpace::Equals(const ColorSpace* colorSpaceA, const ColorSpace* colorSpaceB) {
  if (colorSpaceA == colorSpaceB) {
    return true;
  }
  if (!colorSpaceA || !colorSpaceB) {
    return false;
  }
  if (colorSpaceA->hash() == colorSpaceB->hash()) {
    return true;
  }
  return false;
}

TransferFunction ColorSpace::transferFunction() const {
  return _transferFunction;
}

TransferFunction ColorSpace::inverseTransferFunction() const {
  computeLazyDstFields();
  return _invTransferFunction;
}

void ColorSpace::gamutTransformTo(const ColorSpace* dst, ColorMatrix33* srcToDst) const {
  dst->computeLazyDstFields();
  auto result =
      gfx::skcms_Matrix3x3_concat(reinterpret_cast<const gfx::skcms_Matrix3x3*>(&dst->_fromXYZD50),
                                  reinterpret_cast<const gfx::skcms_Matrix3x3*>(&_toXYZD50));
  *srcToDst = *reinterpret_cast<ColorMatrix33*>(&result);
}

ColorSpace::ColorSpace(const TransferFunction& transferFunction, const ColorMatrix33& toXYZ)
    : _transferFunction(transferFunction), _toXYZD50(toXYZ) {
  _transferFunctionHash = checksum::Hash32(&_transferFunction, 7 * sizeof(float));
  _toXYZD50Hash = checksum::Hash32(&_toXYZD50, 9 * sizeof(float));
}

void ColorSpace::computeLazyDstFields() const {
  if (!isLazyDstFieldsResolved) {

    // Invert 3x3 gamut, defaulting to sRGB if we can't.
    if (!gfx::skcms_Matrix3x3_invert(reinterpret_cast<const gfx::skcms_Matrix3x3*>(&_toXYZD50),
                                     reinterpret_cast<gfx::skcms_Matrix3x3*>(&_fromXYZD50))) {
      ASSERT(gfx::skcms_Matrix3x3_invert(&gfx::skcms_sRGB_profile()->toXYZD50,
                                         reinterpret_cast<gfx::skcms_Matrix3x3*>(&_fromXYZD50)))
    }

    // Invert transfer function, defaulting to sRGB if we can't.
    if (!gfx::skcms_TransferFunction_invert(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&_transferFunction),
            reinterpret_cast<gfx::skcms_TransferFunction*>(&_invTransferFunction))) {
      _invTransferFunction =
          *reinterpret_cast<const TransferFunction*>(gfx::skcms_sRGB_Inverse_TransferFunction());
    }

    isLazyDstFieldsResolved = true;
  }
}

}  // namespace tgfx