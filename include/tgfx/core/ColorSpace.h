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

#include <memory>
#include "tgfx/core/Data.h"

namespace tgfx {

struct Matrix3x3 {
  float values[3][3];
};

struct TransferFunction {
  float g, a, b, c, d, e, f;
};

/**
 *  Describes a color gamut with primaries and a white point.
 */
struct ColorSpacePrimaries {
  float rx;
  float ry;
  float gx;
  float gy;
  float bx;
  float by;
  float wx;
  float wy;

  /**
 *  Convert primaries and a white point to a toXYZD50 matrix, the preferred color gamut
 *  representation of ColorSpace.
 */
  bool toXYZD50(Matrix3x3* toXYZD50) const;
};

////////////////////////////////////////////////////////////////////////////////
// Color primaries defined by ITU-T H.273, table 2. Names are given by the first
// specification referenced in the value's row.

// Rec. ITU-R BT.709-6, value 1.
static constexpr ColorSpacePrimaries Rec709Primaries = {0.64f, 0.33f, 0.3f,    0.6f,
                                                        0.15f, 0.06f, 0.3127f, 0.329f};

// Rec. ITU-R BT.470-6 System M (historical), value 4.
static constexpr ColorSpacePrimaries Rec470SystemMPrimaries = {0.67f, 0.33f, 0.21f, 0.71f,
                                                               0.14f, 0.08f, 0.31f, 0.316f};

// Rec. ITU-R BT.470-6 System B, G (historical), value 5.
static constexpr ColorSpacePrimaries Rec470SystemBGPrimaries = {0.64f, 0.33f, 0.29f,   0.60f,
                                                                0.15f, 0.06f, 0.3127f, 0.3290f};

// Rec. ITU-R BT.601-7 525, value 6.
static constexpr ColorSpacePrimaries Rec601Primaries = {0.630f, 0.340f, 0.310f,  0.595f,
                                                        0.155f, 0.070f, 0.3127f, 0.3290f};

// SMPTE ST 240, value 7 (functionally the same as value 6).
static constexpr ColorSpacePrimaries SMPTE_ST_240Primaries = Rec601Primaries;

// Generic film (colour filters using Illuminant C), value 8.
static constexpr ColorSpacePrimaries GenericFilmPrimaries = {0.681f, 0.319f, 0.243f, 0.692f,
                                                             0.145f, 0.049f, 0.310f, 0.316f};

// Rec. ITU-R BT.2020-2, value 9.
static constexpr ColorSpacePrimaries Rec2020Primaries{0.708f, 0.292f, 0.170f,  0.797f,
                                                      0.131f, 0.046f, 0.3127f, 0.3290f};

// SMPTE ST 428-1, value 10.
static constexpr ColorSpacePrimaries SMPTE_ST_428_1Primaries = {1.f, 0.f, 0.f,       1.f,
                                                                0.f, 0.f, 1.f / 3.f, 1.f / 3.f};

// SMPTE RP 431-2, value 11.
static constexpr ColorSpacePrimaries SMPTE_RP_431_2Primaries = {0.680f, 0.320f, 0.265f, 0.690f,
                                                                0.150f, 0.060f, 0.314f, 0.351f};

// SMPTE EG 432-1, value 12.
static constexpr ColorSpacePrimaries SMPTE_EG_432_1Primaries = {0.680f, 0.320f, 0.265f,  0.690f,
                                                                0.150f, 0.060f, 0.3127f, 0.3290f};

// No corresponding industry specification identified, value 22.
// This is sometimes referred to as EBU 3213-E, but that document doesn't
// specify these values.
static constexpr ColorSpacePrimaries ITU_T_H273_Value22Primaries = {
    0.630f, 0.340f, 0.295f, 0.605f, 0.155f, 0.077f, 0.3127f, 0.3290f};

// Mapping between names of color primaries and the number of the corresponding
// row in ITU-T H.273, table 2.  As above, the constants are named based on the
// first specification referenced in the value's row.
enum class CicpPrimariesId : uint8_t {
  // Value 0 is reserved.
  Rec709 = 1,
  // Value 2 is unspecified.
  // Value 3 is reserved.
  Rec470SystemM = 4,
  Rec470SystemBG = 5,
  Rec601 = 6,
  SMPTE_ST_240 = 7,
  GenericFilm = 8,
  Rec2020 = 9,
  SMPTE_ST_428_1 = 10,
  SMPTE_RP_431_2 = 11,
  SMPTE_EG_432_1 = 12,
  // Values 13-21 are reserved.
  ITU_T_H273_Value22 = 22,
  // Values 23-255 are reserved.
};

// https://www.w3.org/TR/css-color-4/#predefined-prophoto-rgb
static constexpr ColorSpacePrimaries ProPhotoRGBPrimaries = {0.7347f, 0.2653f, 0.1596f,  0.8404f,
                                                             0.0366f, 0.0001f, 0.34567f, 0.35850f};

static constexpr TransferFunction SRGBTF = {
    2.4f, (float)(1 / 1.055), (float)(0.055 / 1.055), (float)(1 / 12.92), 0.04045f, 0.0f, 0.0f};

static constexpr TransferFunction twoDotTwoTF = {2.2f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

static constexpr TransferFunction Rec2020TF = {2.22222f,   0.909672f, 0.0903276f, 0.222222f,
                                               0.0812429f, 0,         0};

////////////////////////////////////////////////////////////////////////////////
// Color primaries defined by ITU-T H.273, table 3. Names are given by the first
// specification referenced in the value's row.

// Rec. ITU-R BT.709-6, value 1.
static constexpr TransferFunction Rec709TF = {
    2.222222222222f, 0.909672415686f, 0.090327584314f, 0.222222222222f, 0.081242858299f, 0.f, 0.f};

// Rec. ITU-R BT.470-6 System M (historical) assumed display gamma 2.2, value 4.
static constexpr TransferFunction Rec470SystemMTF = {2.2f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f};

// Rec. ITU-R BT.470-6 System B, G (historical) assumed display gamma 2.8,
// value 5.
static constexpr TransferFunction Rec470SystemBGTF = {2.8f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f};

// Rec. ITU-R BT.601-7, same as kRec709, value 6.
static constexpr TransferFunction Rec601TF = Rec709TF;

// SMPTE ST 240, value 7.
static constexpr TransferFunction SMPTE_ST_240TF = {
    2.222222222222f, 0.899626676224f, 0.100373323776f, 0.25f, 0.091286342118f, 0.f, 0.f};

// Linear, value 8
static constexpr TransferFunction LinearTF = {1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

// IEC 61966-2-4, value 11, same as Rec709 (but is explicitly extended).
static constexpr TransferFunction IEC61966_2_4TF = Rec709TF;

// IEC 61966-2-1 sRGB, value 13.
static constexpr TransferFunction IEC61966_2_1TF = SRGBTF;

// Rec. ITU-R BT.2020-2 (10-bit system), value 14.
static constexpr TransferFunction Rec2020_10bitTF = Rec709TF;

// Rec. ITU-R BT.2020-2 (12-bit system), value 15.
static constexpr TransferFunction Rec2020_12bitTF = Rec709TF;

// SMPTE ST 428-1, value 17.
static constexpr TransferFunction SMPTE_ST_428_1TF = {2.6f, 1.034080527699f, 0.f, 0.f, 0.f, 0.f,
                                                      0.f};

// Mapping between transfer function names and the number of the corresponding
// row in ITU-T H.273, table 3.  As above, the constants are named based on the
// first specification referenced in the value's row.
enum class CicpTFId : uint8_t {
  // Value 0 is reserved.
  Rec709 = 1,
  // Value 2 is unspecified.
  // Value 3 is reserved.
  Rec470SystemM = 4,
  Rec470SystemBG = 5,
  Rec601 = 6,
  SMPTE_ST_240 = 7,
  Linear = 8,
  // Value 9 is not supported by `SkColorSpace::MakeCICP`.
  // Value 10 is not supported by `SkColorSpace::MakeCICP`.
  IEC61966_2_4 = 11,
  // Value 12 is not supported by `SkColorSpace::MakeCICP`.
  IEC61966_2_1 = 13,
  SRGB = IEC61966_2_1,
  Rec2020_10bit = 14,
  Rec2020_12bit = 15,
  // Value 16 is not supported by `SkColorSpace::MakeCICP`.
  SMPTE_ST_428_1 = 17,
  // Value 18 is not supported by `SkColorSpace::MakeCICP`.
  // Values 19-255 are reserved.
};

// https://w3.org/TR/css-color-4/#valdef-color-prophoto-rgb
// "The transfer curve is a gamma function with a value of 1/1.8"
static constexpr TransferFunction ProPhotoRGBTF = {1.8f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

// https://www.w3.org/TR/css-color-4/#predefined-a98-rgb
static constexpr TransferFunction A98RGBTF = twoDotTwoTF;

#define TGFXFixedToFloat(x) ((x)*1.52587890625e-5f)
static constexpr Matrix3x3 SRGBMat = {{
    // ICC fixed-point (16.16) representation, taken from skcms. Please keep them exactly in sync.
    // 0.436065674f, 0.385147095f, 0.143066406f,
    // 0.222488403f, 0.716873169f, 0.060607910f,
    // 0.013916016f, 0.097076416f, 0.714096069f,
    {TGFXFixedToFloat(0x6FA2), TGFXFixedToFloat(0x6299), TGFXFixedToFloat(0x24A0)},
    {TGFXFixedToFloat(0x38F5), TGFXFixedToFloat(0xB785), TGFXFixedToFloat(0x0F84)},
    {TGFXFixedToFloat(0x0390), TGFXFixedToFloat(0x18DA), TGFXFixedToFloat(0xB6CF)},
}};

static constexpr Matrix3x3 AdobeRGBMat = {{
    // ICC fixed-point (16.16) repesentation of:
    // 0.60974, 0.20528, 0.14919,
    // 0.31111, 0.62567, 0.06322,
    // 0.01947, 0.06087, 0.74457,
    {TGFXFixedToFloat(0x9c18), TGFXFixedToFloat(0x348d), TGFXFixedToFloat(0x2631)},
    {TGFXFixedToFloat(0x4fa5), TGFXFixedToFloat(0xa02c), TGFXFixedToFloat(0x102f)},
    {TGFXFixedToFloat(0x04fc), TGFXFixedToFloat(0x0f95), TGFXFixedToFloat(0xbe9c)},
}};

static constexpr Matrix3x3 DisplayP3Mat = {{
    {0.515102f, 0.291965f, 0.157153f},
    {0.241182f, 0.692236f, 0.0665819f},
    {-0.00104941f, 0.0418818f, 0.784378f},
}};

static constexpr Matrix3x3 Rec2020Mat = {{
    {0.673459f, 0.165661f, 0.125100f},
    {0.279033f, 0.675338f, 0.0456288f},
    {-0.00193139f, 0.0299794f, 0.797162f},
}};

static constexpr Matrix3x3 XYZMat = {{
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
}};

class ColorSpace : public std::enable_shared_from_this<ColorSpace> {
 public:
  /**
   *  Create the sRGB color space.
   */
  static std::shared_ptr<ColorSpace> MakeSRGB();

  /**
   *  Colorspace with the sRGB primaries, but a linear (1.0) gamma.
   */
  static std::shared_ptr<ColorSpace> MakeSRGBLinear();

  /**
   *  Create an ColorSpace from a transfer function and a row-major 3x3 transformation to XYZ.
   */
  static std::shared_ptr<ColorSpace> MakeRGB(const TransferFunction& transferFn,
                                             const Matrix3x3& toXYZ);

  /**
   *  Create an ColorSpace from code points specified in Rec. ITU-T H.273.
   *  Null will be returned for invalid or unsupported combination of code
   *  points.
   *
   *  Parameters:
   *
   * - `colorPrimaries` identifies an entry in Rec. ITU-T H.273, Table 2.
   * - `transferCharacteristics` identifies an entry in Rec. ITU-T H.273, Table 3.
   *
   * `ColorSpace` (and the underlying `skcms_ICCProfile`) only supports RGB
   *  color spaces and therefore this function does not take a
   * `matrix_coefficients` parameter - the caller is expected to verify that
   * `matrix_coefficients` is `0`.
   *
   * Narrow range images are extremely rare - see
   * https://github.com/w3c/png/issues/312#issuecomment-2327349614.  Therefore
   * this function doesn't take a `video_full_range_flag` - the caller is
   * expected to verify that it is `1` (indicating a full range image).
   */
  static std::shared_ptr<ColorSpace> MakeCICP(CicpPrimariesId colorPrimaries,
                                              CicpTFId transferCharacteristics);

  /**
 *  Create an ColorSpace from a ICC data.
 */
  static std::shared_ptr<ColorSpace> MakeFromICC(const void* data, size_t size);

  /**
   *  Returns true if the color space gamma is near enough to be approximated as sRGB.
   */
  bool gammaCloseToSRGB() const;

  /**
   *  Returns true if the color space gamma is linear.
   */
  bool gammaIsLinear() const;

  /**
   *  Sets |fn| to the transfer function from this color space. Returns true if the transfer
   *  function can be represented as coefficients to the standard ICC 7-parameter equation.
   *  Returns false otherwise (eg, PQ, HLG).
   */
  bool isNumericalTransferFn(TransferFunction* fn) const;

  /**
   *  Returns true and sets |toXYZD50|.
   */
  bool toXYZD50(Matrix3x3* toXYZD50) const;

  /**
   *  Returns a hash of the gamut transformation to XYZ D50. Allows for fast equality checking
   *  of gamuts, at the (very small) risk of collision.
   */
  uint32_t toXYZD50Hash() const {
    return _toXYZD50Hash;
  }

  /**
   *  Returns a color space with the same gamut as this one, but with a linear gamma.
   */
  std::shared_ptr<ColorSpace> makeLinearGamma() const;

  /**
   *  Returns a color space with the same gamut as this one, but with the sRGB transfer
   *  function.
   */
  std::shared_ptr<ColorSpace> makeSRGBGamma() const;

  /**
   *  Returns a color space with the same transfer function as this one, but with the primary
   *  colors rotated. In other words, this produces a new color space that maps RGB to GBR
   *  (when applied to a source), and maps RGB to BRG (when applied to a destination).
   *
   *  This is used for testing, to construct color spaces that have severe and testable behavior.
   */
  std::shared_ptr<ColorSpace> makeColorSpin() const;

  /**
   *  Returns true if the color space is sRGB.
   *  Returns false otherwise.
   *
*  This allows a little bit of tolerance, given that we might see small numerical error
   *  in some cases: converting ICC fixed point to float, converting white point to D50,
   *  rounding decisions on transfer function and matrix.
   *
   *  This does not consider a 2.2f exponential transfer function to be sRGB. While these
   *  functions are similar (and it is sometimes useful to consider them together), this
   *  function checks for logical equality.
   */
  bool isSRGB() const;

  /**
   *  Returns a serialized representation of this color space.
   */
  std::shared_ptr<Data> serialize() const;

  /**
   *  If |memory| is nullptr, returns the size required to serialize.
   *  Otherwise, serializes into |memory| and returns the size.The memory size must be greater than
   *  or equal to the returned size.
   */
  size_t writeToMemory(void* memory) const;

  /**
   * Make an ICCProfile from the colorSpace.
   */
  std::shared_ptr<Data> toICCProfile() const;

  static std::shared_ptr<ColorSpace> Deserialize(const void* data, size_t length);

  /**
   *  If both are null, we return true. If one is null and the other is not, we return false.
   *  If both are non-null, we do a deeper compare.
   */
  static bool Equals(const ColorSpace*, const ColorSpace*);

  void transferFn(TransferFunction* fn) const;
  void invTransferFn(TransferFunction* fn) const;
  void gamutTransformTo(const ColorSpace* dst, Matrix3x3* srcToDst) const;

  uint32_t transferFnHash() const {
    return _transferFnHash;
  }
  uint64_t hash() const {
    return (uint64_t)_transferFnHash << 32 | _toXYZD50Hash;
  }

 private:
  ColorSpace(const TransferFunction& transferFn, const Matrix3x3& toXYZ);

  void computeLazyDstFields() const;

  uint32_t _transferFnHash;
  uint32_t _toXYZD50Hash;

  TransferFunction _transferFn;
  Matrix3x3 _toXYZD50;

  mutable TransferFunction _invTransferFn;
  mutable Matrix3x3 _fromXYZD50;
  mutable bool isLazyDstFieldsResolved = false;
};

}  // namespace tgfx