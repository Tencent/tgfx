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
#if !defined(TGFX_IMAGE_RESIZE_DO_HORIZONTALS) && !defined(TGFX_IMAGE_RESIZE_DO_VERTICALS) && \
    !defined(TGFX_IMAGE_RESIZE_DO_CODERS)
#include "ImageResize.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace tgfx {

#ifndef SOURCE_FILE
#define SOURCE_FILE "ImageResize.cpp"
#endif

#define MAX_UINT8_AS_FLOAT 255.0f
#define MAX_UINT16_AS_FLOAT 65535.0f
#define MAX_UINT8_AS_FLOAT_INVERTED 3.9215689e-03f   // (1.0f/255.0f)
#define MAX_UINT16_AS_FLOAT_INVERTED 1.5259022e-05f  // (1.0f/65535.0f)
#define SMALL_FLOAT \
  ((float)1 / (1 << 20) / (1 << 20) / (1 << 20) / (1 << 20) / (1 << 20) / (1 << 20))

#ifdef _MSC_VER

#define TGFX_INLINE __forceinline

#else

#define TGFX_INLINE __inline__

#endif

// min/max friendly
#define TGFX_CLAMP(x, xmin, xmax)   \
  for (;;) {                        \
    if ((x) < (xmin)) (x) = (xmin); \
    if ((x) > (xmax)) (x) = (xmax); \
    break;                          \
  }

static TGFX_INLINE int TGFX_MIN(int a, int b) {
  return a < b ? a : b;
}

static TGFX_INLINE int TGFX_MAX(int a, int b) {
  return a > b ? a : b;
}

static float SRGBUcharToLinearFloat[256] = {
    0.000000f, 0.000304f, 0.000607f, 0.000911f, 0.001214f, 0.001518f, 0.001821f, 0.002125f,
    0.002428f, 0.002732f, 0.003035f, 0.003347f, 0.003677f, 0.004025f, 0.004391f, 0.004777f,
    0.005182f, 0.005605f, 0.006049f, 0.006512f, 0.006995f, 0.007499f, 0.008023f, 0.008568f,
    0.009134f, 0.009721f, 0.010330f, 0.010960f, 0.011612f, 0.012286f, 0.012983f, 0.013702f,
    0.014444f, 0.015209f, 0.015996f, 0.016807f, 0.017642f, 0.018500f, 0.019382f, 0.020289f,
    0.021219f, 0.022174f, 0.023153f, 0.024158f, 0.025187f, 0.026241f, 0.027321f, 0.028426f,
    0.029557f, 0.030713f, 0.031896f, 0.033105f, 0.034340f, 0.035601f, 0.036889f, 0.038204f,
    0.039546f, 0.040915f, 0.042311f, 0.043735f, 0.045186f, 0.046665f, 0.048172f, 0.049707f,
    0.051269f, 0.052861f, 0.054480f, 0.056128f, 0.057805f, 0.059511f, 0.061246f, 0.063010f,
    0.064803f, 0.066626f, 0.068478f, 0.070360f, 0.072272f, 0.074214f, 0.076185f, 0.078187f,
    0.080220f, 0.082283f, 0.084376f, 0.086500f, 0.088656f, 0.090842f, 0.093059f, 0.095307f,
    0.097587f, 0.099899f, 0.102242f, 0.104616f, 0.107023f, 0.109462f, 0.111932f, 0.114435f,
    0.116971f, 0.119538f, 0.122139f, 0.124772f, 0.127438f, 0.130136f, 0.132868f, 0.135633f,
    0.138432f, 0.141263f, 0.144128f, 0.147027f, 0.149960f, 0.152926f, 0.155926f, 0.158961f,
    0.162029f, 0.165132f, 0.168269f, 0.171441f, 0.174647f, 0.177888f, 0.181164f, 0.184475f,
    0.187821f, 0.191202f, 0.194618f, 0.198069f, 0.201556f, 0.205079f, 0.208637f, 0.212231f,
    0.215861f, 0.219526f, 0.223228f, 0.226966f, 0.230740f, 0.234551f, 0.238398f, 0.242281f,
    0.246201f, 0.250158f, 0.254152f, 0.258183f, 0.262251f, 0.266356f, 0.270498f, 0.274677f,
    0.278894f, 0.283149f, 0.287441f, 0.291771f, 0.296138f, 0.300544f, 0.304987f, 0.309469f,
    0.313989f, 0.318547f, 0.323143f, 0.327778f, 0.332452f, 0.337164f, 0.341914f, 0.346704f,
    0.351533f, 0.356400f, 0.361307f, 0.366253f, 0.371238f, 0.376262f, 0.381326f, 0.386430f,
    0.391573f, 0.396755f, 0.401978f, 0.407240f, 0.412543f, 0.417885f, 0.423268f, 0.428691f,
    0.434154f, 0.439657f, 0.445201f, 0.450786f, 0.456411f, 0.462077f, 0.467784f, 0.473532f,
    0.479320f, 0.485150f, 0.491021f, 0.496933f, 0.502887f, 0.508881f, 0.514918f, 0.520996f,
    0.527115f, 0.533276f, 0.539480f, 0.545725f, 0.552011f, 0.558340f, 0.564712f, 0.571125f,
    0.577581f, 0.584078f, 0.590619f, 0.597202f, 0.603827f, 0.610496f, 0.617207f, 0.623960f,
    0.630757f, 0.637597f, 0.644480f, 0.651406f, 0.658375f, 0.665387f, 0.672443f, 0.679543f,
    0.686685f, 0.693872f, 0.701102f, 0.708376f, 0.715694f, 0.723055f, 0.730461f, 0.737911f,
    0.745404f, 0.752942f, 0.760525f, 0.768151f, 0.775822f, 0.783538f, 0.791298f, 0.799103f,
    0.806952f, 0.814847f, 0.822786f, 0.830770f, 0.838799f, 0.846873f, 0.854993f, 0.863157f,
    0.871367f, 0.879622f, 0.887923f, 0.896269f, 0.904661f, 0.913099f, 0.921582f, 0.930111f,
    0.938686f, 0.947307f, 0.955974f, 0.964686f, 0.973445f, 0.982251f, 0.991102f, 1.0f};

union FP32 {
  unsigned int u;
  float f;
};

static const uint32_t FP32ToSRGB8Tab4[104] = {
    0x0073000d, 0x007a000d, 0x0080000d, 0x0087000d, 0x008d000d, 0x0094000d, 0x009a000d, 0x00a1000d,
    0x00a7001a, 0x00b4001a, 0x00c1001a, 0x00ce001a, 0x00da001a, 0x00e7001a, 0x00f4001a, 0x0101001a,
    0x010e0033, 0x01280033, 0x01410033, 0x015b0033, 0x01750033, 0x018f0033, 0x01a80033, 0x01c20033,
    0x01dc0067, 0x020f0067, 0x02430067, 0x02760067, 0x02aa0067, 0x02dd0067, 0x03110067, 0x03440067,
    0x037800ce, 0x03df00ce, 0x044600ce, 0x04ad00ce, 0x051400ce, 0x057b00c5, 0x05dd00bc, 0x063b00b5,
    0x06970158, 0x07420142, 0x07e30130, 0x087b0120, 0x090b0112, 0x09940106, 0x0a1700fc, 0x0a9500f2,
    0x0b0f01cb, 0x0bf401ae, 0x0ccb0195, 0x0d950180, 0x0e56016e, 0x0f0d015e, 0x0fbc0150, 0x10630143,
    0x11070264, 0x1238023e, 0x1357021d, 0x14660201, 0x156601e9, 0x165a01d3, 0x174401c0, 0x182401af,
    0x18fe0331, 0x1a9602fe, 0x1c1502d2, 0x1d7e02ad, 0x1ed4028d, 0x201a0270, 0x21520256, 0x227d0240,
    0x239f0443, 0x25c003fe, 0x27bf03c4, 0x29a10392, 0x2b6a0367, 0x2d1d0341, 0x2ebe031f, 0x304d0300,
    0x31d105b0, 0x34a80555, 0x37520507, 0x39d504c5, 0x3c37048b, 0x3e7c0458, 0x40a8042a, 0x42bd0401,
    0x44c20798, 0x488e071e, 0x4c1c06b6, 0x4f76065d, 0x52a50610, 0x55ac05cc, 0x5892058f, 0x5b590559,
    0x5e0c0a23, 0x631c0980, 0x67db08f6, 0x6c55087f, 0x70940818, 0x74a007bd, 0x787d076c, 0x7c330723,
};

static TGFX_INLINE uint8_t LinearToSRGBUchar(float in) {
  static const FP32 almostone = {0x3f7fffff};  // 1-eps
  static const FP32 minval = {(127 - 13) << 23};
  uint32_t tab, bias, scale, t;
  FP32 f;

  // Clamp to [2^(-13), 1-eps]; these two values map to 0 and 1, respectively. The tests are
  // carefully written so that NaNs map to 0, same as in the reference implementation.
  if (!(in > minval.f))  // written this way to catch NaNs
    return 0;
  if (in > almostone.f) return 255;

  // Do the table lookup and unpack bias, scale
  f.f = in;
  tab = FP32ToSRGB8Tab4[(f.u - minval.u) >> 20];
  bias = (tab >> 16) << 9;
  scale = tab & 0xffff;

  // Grab next-highest mantissa bits and perform linear interpolation
  t = (f.u >> 12) & 0xff;
  return static_cast<unsigned char>(((bias + scale * t) >> 16));
}

union FP16 {
  unsigned short u;
};

static TGFX_INLINE float HalfToFloat(FP16 h) {
  static const FP32 magic = {(254 - 15) << 23};
  static const FP32 wasInfnan = {(127 + 16) << 23};
  FP32 o;

  o.u = static_cast<unsigned int>((h.u & 0x7fff)) << 13;  // exponent/mantissa bits
  o.f *= magic.f;                                         // exponent adjust
  if (o.f >= wasInfnan.f)                                 // make sure Inf/NaN survive
    o.u |= 255 << 23;
  o.u |= static_cast<unsigned int>((h.u & 0x8000)) << 16;  // sign bit
  return o.f;
}

static TGFX_INLINE FP16 FloatToHalf(float val) {
  FP32 f32infty = {255 << 23};
  FP32 f16max = {(127 + 16) << 23};
  FP32 denormMagic = {((127 - 15) + (23 - 10) + 1) << 23};
  unsigned int signMask = 0x80000000u;
  FP16 o = {0};
  FP32 f;
  unsigned int sign;

  f.f = val;
  sign = f.u & signMask;
  f.u ^= sign;

  if (f.u >= f16max.u)                           // result is Inf or NaN (all exponent bits set)
    o.u = (f.u > f32infty.u) ? 0x7e00 : 0x7c00;  // NaN->qNaN and Inf->Inf
  else                                           // (De)normalized number or zero
  {
    if (f.u < (113 << 23))  // resulting FP16 is subnormal or zero
    {
      // use a magic value to align our 10 mantissa bits at the bottom of
      // the float. as long as FP addition is round-to-nearest-even this
      // just works.
      f.f += denormMagic.f;
      // and one integer subtract of the bias later, we have our final float!
      o.u = static_cast<unsigned short>(f.u - denormMagic.u);
    } else {
      unsigned int mant_odd = (f.u >> 13) & 1;  // resulting mantissa is odd
      // update exponent, rounding bias part 1
      f.u = f.u + ((15u - 127) << 23) + 0xfff;
      // rounding bias part 2
      f.u += mant_odd;
      // take the bits!
      o.u = static_cast<unsigned short>(f.u >> 13);
    }
  }

  o.u |= sign >> 16;
  return o;
}

struct Contributors {
  int n0;  // First contributing pixel
  int n1;  // Last contributing pixel
};

struct ScaleInfo {
  int inputFullSize;
  int outputSubSize;
  float scale;
  float invScale;
  float pixelShift;  // starting shift in output pixel space (in pixels)
  int scaleIsRational;
  uint32_t scaleNumerator, scaleDenominator;
};

struct FilterExtentInfo {
  int lowest;   // First sample index for whole filter
  int highest;  // Last sample index for whole filter
  int widest;   // widest single set of samples for an output
};

struct Sampler {
  Contributors* contributors;
  float* coefficients;
  Contributors* gatherPrescatterContibutors;
  float* gatherPrescatterCoefficients;
  ScaleInfo scaleInfo;
  float support;
  int coefficientWidth;
  int filterPixelWidth;
  int filterPixelMargin;
  int numContibutors;
  int contributorsSize;
  int coefficientsSize;
  FilterExtentInfo extentInfo;
  int isGather;  // 0 = scatter, 1 = gather with scale >= 1, 2 = gather with scale < 1
  int gatherPrescatterNumContributors;
  int gatherPrescatterCoefficientWidth;
  int gatherPrescatterContibutorsSize;
  int gatherPrescatterCoefficientsSize;
};

struct Span {
  int n0;                   // First pixel of decode buffer to write to
  int n1;                   // Last pixel of decode that will be written to
  int pixelOffsetForInput;  // Pixel offset into input_scanline
};

struct Extents {
  Contributors conservative;
  int edgeSizes[2];  // this can be less than filterPixelMargin, if the filter and scaling falls off
  Span spans[2];     // can be two spans, if doing input subrect with clamp mode WRAP
};

struct PerSplitInfo {
  float* decodeBuffer;

  int ringBufferFirstScanline;
  int ringBufferLastScanline;
  int ringBufferBeginIndex;  // first_scanline is at this index in the ring buffer
  int startOutputY, endOutputY;
  int startInputY, endInputY;  // used in scatter only
  float* ringBuffer;           // one big buffer that we index into

  float* verticalBuffer;

  char noCacheStaddle[64];
};

typedef float* DecodePixelsFunc(float* decode, int widthTimesChannels, void const* input);
typedef void AlphaWeightFunc(float* decodeBuffer, int widthTimesChannels);
typedef void HorizontalGatherChannelsFunc(float* outputBuffer, unsigned int outputSubSize,
                                          float const* decodeBuffer,
                                          Contributors const* horizontalContributors,
                                          float const* horizontalCoefficients,
                                          int coefficientWidth);
typedef void AlphaUnweightFunc(float* encodeBuffer, int widthTimesChannels);
typedef void EncodePixelsFunc(void* output, int widthTimesChannels, float const* encode);

// the internal pixel layout enums are in a different order, so we can easily do range comparisons
// of types the public pixel layout is ordered in a way that if you cast num_channels (1-4) to the
// enum, you get something sensible
enum class InternalPixelLayout {
  CHANNEL_1 = 0,
  CHANNEL_2 = 1,
  RGB = 2,
  BGR = 3,
  CHANNEL_4 = 4,

  RGBA = 5,
  BGRA = 6,
  ARGB = 7,
  ABGR = 8,
  RA = 9,
  AR = 10,

  RGBA_PM = 11,
  BGRA_PM = 12,
  ARGB_PM = 13,
  ABGR_PM = 14,
  RA_PM = 15,
  AR_PM = 16,
};

struct ResizeInfo {
  Sampler horizontal;
  Sampler vertical;

  void const* inputData;
  void* outputData;

  int inputStrideBytes;
  int outputStrideBytes;
  int ringBufferLengthBytes;  // The length of an individual entry in the ring buffer. The total number of ring buffers is GetFilterPixelWidth(filter)
  int ringBufferNumEntries;   // Total number of entries in the ring buffer.

  DataType inputType;
  DataType outputType;

  Extents scanlineExtents;

  void* allocedMem;
  PerSplitInfo*
      splitInfo;  // by default 1, but there will be N of these allocated based on the thread init you did

  DecodePixelsFunc* decodePixels;
  AlphaWeightFunc* alphaWeight;
  HorizontalGatherChannelsFunc* horizontalGatherchannels;
  AlphaUnweightFunc* alphaUnweight;
  EncodePixelsFunc* encodePixels;

  int allocRingBufferNumEntries;  // Number of entries in the ring buffer that will be allocated
  int splits;                     // count of splits

  InternalPixelLayout inputPixelLayoutInternal;
  InternalPixelLayout outputPixelLayoutInternal;

  int inputColorAndType;
  int offsetX, offsetY;  // offset within outputData
  int verticalFirst;
  int channels;
  int effectiveChannels;  // same as channels, except on RGBA/ARGB (7), or XA/AX (3)
  size_t allocedTotal;
};

struct ResizeData {
  void const* inputPixels;
  int inputW, intputH;
  double inputS0, inputT0, inputS1, inputT1;
  void* outputPixels;
  int outputW, outputH;
  int outputSubX, outputSubY, outputSubW, outputSubH;
  int inputStrideInBytes;
  int outputStrideInBytes;
  int splits;
  int needsRebuild;
  int calledAlloc;
  PixelLayout inputPixelLayoutPublic;
  PixelLayout outputPixelLaytoutPublic;
  DataType inputDataType;
  DataType outputDataType;
  ResizeInfo* samplers;
};

// layout lookups - must match InternalPixelLayout
static unsigned char PixelChannels[] = {
    1, 2, 3, 3, 4,     // 1ch, 2ch, rgb, bgr, 4ch
    4, 4, 4, 4, 2, 2,  // RGBA,BGRA,ARGB,ABGR,RA,AR
    4, 4, 4, 4, 2, 2,  // RGBA_PM,BGRA_PM,ARGB_PM,ABGR_PM,RA_PM,AR_PM
};

static int CheckOutputStuff(void* outputPixels, int typeSize, int outputW, int outputH,
                            int outputStrideInBytes, InternalPixelLayout pixelLayout) {
  size_t size;
  int pitch;

  pitch = outputW * typeSize * PixelChannels[static_cast<size_t>(pixelLayout)];
  if (pitch == 0) return 0;

  if (outputStrideInBytes == 0) outputStrideInBytes = pitch;

  if (outputStrideInBytes < pitch) return 0;

  size = static_cast<size_t>(outputStrideInBytes) * static_cast<size_t>(outputH);
  if (size == 0) return 0;

  if (outputPixels == 0) {
    return 0;
  }

  return 1;
}

// must match tgfx_datatype
static unsigned char TypeSize[] = {
    // TGFX_TYPE_UINT8,TGFX_TYPE_UINT8_SRGB,TGFX_TYPE_UINT8_SRGB_ALPHA,TGFX_TYPE_UINT16,
    // TGFX_TYPE_FLOAT,TGFX_TYPE_HALF_FLOAT
    1, 1, 1, 2, 4, 2};

// the internal pixel layout enums are in a different order, so we can easily do range comparisons
// of types the public pixel layout is ordered in a way that if you cast num_channels (1-4) to the
// enum, you get something sensible
static InternalPixelLayout PixelLayoutConvertPublicToInternal[] = {
    InternalPixelLayout::BGR,     InternalPixelLayout::CHANNEL_1, InternalPixelLayout::CHANNEL_2,
    InternalPixelLayout::RGB,     InternalPixelLayout::RGBA,      InternalPixelLayout::CHANNEL_4,
    InternalPixelLayout::BGRA,    InternalPixelLayout::ARGB,      InternalPixelLayout::ABGR,
    InternalPixelLayout::RA,      InternalPixelLayout::AR,        InternalPixelLayout::RGBA_PM,
    InternalPixelLayout::BGRA_PM, InternalPixelLayout::ARGB_PM,   InternalPixelLayout::ABGR_PM,
    InternalPixelLayout::RA_PM,   InternalPixelLayout::AR_PM};

static void InitAndSetLayout(ResizeData* resize, PixelLayout pixelLayout, DataType dataType) {
  resize->samplers = 0;
  resize->calledAlloc = 0;
  resize->inputS0 = 0;
  resize->inputT0 = 0;
  resize->inputS1 = 1;
  resize->inputT1 = 1;
  resize->outputSubX = 0;
  resize->outputSubY = 0;
  resize->outputSubW = resize->outputW;
  resize->outputSubH = resize->outputH;
  resize->inputDataType = dataType;
  resize->outputDataType = dataType;
  resize->inputPixelLayoutPublic = pixelLayout;
  resize->outputPixelLaytoutPublic = pixelLayout;
  resize->needsRebuild = 1;
}

static void ResizeInit(ResizeData* resize, const void* inputPixels, int inputW, int inputH,
                       int inputStrideInBytes,  // stride can be zero
                       void* outputPixels, int outputW, int outputH,
                       int outputStrideInBytes,  // stride can be zero
                       PixelLayout pixelLayout, DataType dataType) {
  resize->inputPixels = inputPixels;
  resize->inputW = inputW;
  resize->intputH = inputH;
  resize->inputStrideInBytes = inputStrideInBytes;
  resize->outputPixels = outputPixels;
  resize->outputW = outputW;
  resize->outputH = outputH;
  resize->outputStrideInBytes = outputStrideInBytes;

  InitAndSetLayout(resize, pixelLayout, dataType);
}

static void FreeInternalMem(ResizeInfo* info) {
#define FREE_AND_CLEAR(ptr) \
  {                         \
    if (ptr) {              \
      void* p = (ptr);      \
      (ptr) = 0;            \
      free(p);              \
    }                       \
  }
  if (info) {
    FREE_AND_CLEAR(info->allocedMem);
  }
#undef FREE_AND_CLEAR
}

void FreeSamplers(ResizeData* resize) {
  if (resize->samplers) {
    FreeInternalMem(resize->samplers);
    resize->samplers = 0;
    resize->calledAlloc = 0;
  }
}

static void Clip(int* outx, int* outsubw, int outw, double* u0, double* u1) {
  double per, adj;
  int over;

  // do left/top edge
  if (*outx < 0) {
    per = ((double)*outx) / ((double)*outsubw);  // is negative
    adj = per * (*u1 - *u0);
    *u0 -= adj;  // increases u0
    *outx = 0;
  }

  // do right/bot edge
  over = outw - (*outx + *outsubw);
  if (over < 0) {
    per = ((double)over) / ((double)*outsubw);  // is negative
    adj = per * (*u1 - *u0);
    *u1 += adj;  // decrease u1
    *outsubw = outw - *outx;
  }
}

// converts a double to a rational that has less than one float bit of error (returns 0 if unable to
// do so). limitDenom (1) or limit numer (0)
static int DoubleToRational(double f, uint32_t limit, uint32_t* numer, uint32_t* denom,
                            int limitDenom) {
  double err;
  uint64_t top, bot;
  uint64_t numerLast = 0;
  uint64_t denomLast = 1;
  uint64_t numerEstimate = 1;
  uint64_t denomEstimate = 0;

  // scale to past float error range
  top = (uint64_t)(f * (double)(1 << 25));
  bot = 1 << 25;

  // keep refining, but usually stops in a few loops - usually 5 for bad cases
  for (;;) {
    uint64_t est, temp;

    // hit limit, break out and do best full range estimate
    if (((limitDenom) ? denomEstimate : numerEstimate) >= limit) break;

    // is the current error less than 1 bit of a float? if so, we're done
    if (denomEstimate) {
      err = ((double)numerEstimate / (double)denomEstimate) - f;
      if (err < 0.0) err = -err;
      if (err < (1.0 / (double)(1 << 24))) {
        // yup, found it
        *numer = (uint32_t)numerEstimate;
        *denom = (uint32_t)denomEstimate;
        return 1;
      }
    }

    // no more refinement bits left? break out and do full range estimate
    if (bot == 0) break;

    // gcd the estimate bits
    est = top / bot;
    temp = top % bot;
    top = bot;
    bot = temp;

    // move remainders
    temp = est * denomEstimate + denomLast;
    denomLast = denomEstimate;
    denomEstimate = temp;

    // move remainders
    temp = est * numerEstimate + numerLast;
    numerLast = numerEstimate;
    numerEstimate = temp;
  }

  // we didn't fine anything good enough for float, use a full range estimate
  if (limitDenom) {
    numerEstimate = (uint64_t)(f * (double)limit + 0.5);
    denomEstimate = limit;
  } else {
    numerEstimate = limit;
    denomEstimate = (uint64_t)(((double)limit / f) + 0.5);
  }

  *numer = (uint32_t)numerEstimate;
  *denom = (uint32_t)denomEstimate;

  err = (denomEstimate) ? (((double)(uint32_t)numerEstimate / (double)(uint32_t)denomEstimate) - f)
                        : 1.0;
  if (err < 0.0) err = -err;
  return (err < (1.0 / (double)(1 << 24))) ? 1 : 0;
}

static float FilterTrapezoid(float x, float scale) {
  float halfscale = scale / 2;
  float t = 0.5f + halfscale;
  assert(scale <= 1);

  if (x < 0.0f) x = -x;

  if (x >= t) return 0.0f;
  else {
    float r = 0.5f - halfscale;
    if (x <= r) return 1.0f;
    else
      return (t - x) / scale;
  }
}

static float SupportTrapezoid(float scale) {
  return 0.5f + scale / 2.0f;
}

static int CalculateRegionTransform(ScaleInfo* scaleInfo, int outputFullRange, int* outputOffset,
                                    int outputSubRange, int inputFullRange, double inputS0,
                                    double inputS1) {
  double outputRange, inputRange, outputS, inputS, ratio, scale;

  inputS = inputS1 - inputS0;

  // null area
  if ((outputFullRange == 0) || (inputFullRange == 0) || (outputSubRange == 0) ||
      (inputS <= SMALL_FLOAT))
    return 0;

  // are either of the ranges completely out of bounds?
  if ((*outputOffset >= outputFullRange) || ((*outputOffset + outputSubRange) <= 0) ||
      (inputS0 >= (1.0f - SMALL_FLOAT)) || (inputS1 <= SMALL_FLOAT))
    return 0;

  outputRange = (double)outputFullRange;
  inputRange = (double)inputFullRange;

  outputS = ((double)outputSubRange) / outputRange;

  // figure out the scaling to use
  ratio = outputS / inputS;

  // save scale before clipping
  scale = (outputRange / inputRange) * ratio;
  scaleInfo->scale = (float)scale;
  scaleInfo->invScale = (float)(1.0 / scale);

  // clip output area to left/right output edges (and adjust input area)
  Clip(outputOffset, &outputSubRange, outputFullRange, &inputS0, &inputS1);

  // recalc input area
  inputS = inputS1 - inputS0;

  // after clipping do we have zero input area?
  if (inputS <= SMALL_FLOAT) return 0;

  // calculate and store the starting source offsets in output pixel space
  scaleInfo->pixelShift = (float)(inputS0 * ratio * outputRange);

  scaleInfo->scaleIsRational =
      DoubleToRational(scale, (scale <= 1.0) ? (uint32_t)outputFullRange : (uint32_t)inputFullRange,
                       &scaleInfo->scaleNumerator, &scaleInfo->scaleDenominator, (scale >= 1.0));

  scaleInfo->inputFullSize = inputFullRange;
  scaleInfo->outputSubSize = outputSubRange;

  return 1;
}
#define TGFX_FORCE_GATHER_FILTER_SCANLINES_AMOUNT 32

// This is the maximum number of input samples that can affect an output sample with the given
// filter from the output pixel's perspective
static int GetFilterPixelWidth(float scale) {
  if (scale >= (1.0f - SMALL_FLOAT))  // upscale
    return (int)ceilf(SupportTrapezoid(1.0f / scale) * 2.0f);
  else
    return (int)ceilf(SupportTrapezoid(scale) * 2.0f / scale);
}

// this is how many coefficents per run of the filter (which is different from the filterPixelWidth
// depending on if we are scattering or gathering)
static int GetCoefficientWidth(Sampler* samp, int isGather) {
  float scale = samp->scaleInfo.scale;

  switch (isGather) {
    case 1:
      return (int)ceilf(SupportTrapezoid(1.0f / scale) * 2.0f);
    case 2:
      return (int)ceilf(SupportTrapezoid(scale) * 2.0f / scale);
    case 0:
      return (int)ceilf(SupportTrapezoid(scale) * 2.0f);
    default:
      assert((isGather >= 0) && (isGather <= 2));
      return 0;
  }
}

static int GetContirbutors(Sampler* samp, int isGather) {
  if (isGather) return samp->scaleInfo.outputSubSize;
  else
    return (samp->scaleInfo.inputFullSize + samp->filterPixelMargin * 2);
}

#define TGFX_INPUT_CALLBACK_PADDING 3

static void SetSampler(Sampler* samp, ScaleInfo* scaleInfo, int alwaysGather) {
  samp->filterPixelWidth = GetFilterPixelWidth(scaleInfo->scale);
  // Gather is always better, but in extreme downsamples, you have to most or all of the data in
  // memory. For horizontal, we always have all the pixels, so we always use gather here
  // (alwaysGather==1). For vertical, we use gather if scaling up (which means we will have
  // samp->filterPixelWidth scanlines in memory at once).
  samp->isGather = 0;
  if (scaleInfo->scale >= (1.0f - SMALL_FLOAT)) samp->isGather = 1;
  else if ((alwaysGather) || (samp->filterPixelWidth <= TGFX_FORCE_GATHER_FILTER_SCANLINES_AMOUNT))
    samp->isGather = 2;

  // pre calculate stuff based on the above
  samp->coefficientWidth = GetCoefficientWidth(samp, samp->isGather);

  // This is how much to expand buffers to account for filters seeking outside
  // the image boundaries.
  samp->filterPixelMargin = samp->filterPixelWidth / 2;

  samp->numContibutors = GetContirbutors(samp, samp->isGather);

  samp->contributorsSize = samp->numContibutors * (int)sizeof(Contributors);
  samp->coefficientsSize =
      samp->numContibutors * samp->coefficientWidth * (int)sizeof(float) +
      (int)sizeof(float) * TGFX_INPUT_CALLBACK_PADDING;  // extra sizeof(float) is padding

  samp->gatherPrescatterContibutors = 0;
  samp->gatherPrescatterCoefficients = 0;
  if (samp->isGather == 0) {
    samp->gatherPrescatterCoefficientWidth = samp->filterPixelWidth;
    samp->gatherPrescatterNumContributors = GetContirbutors(samp, 2);
    samp->gatherPrescatterContibutorsSize =
        samp->gatherPrescatterNumContributors * (int)sizeof(Contributors);
    samp->gatherPrescatterCoefficientsSize = samp->gatherPrescatterNumContributors *
                                             samp->gatherPrescatterCoefficientWidth *
                                             (int)sizeof(float);
  }
}

static void CalculateInPixelRange(int* firstPixel, int* lastPixel, float outPixelCenter,
                                  float outFilterRadius, float invScale, float outShift) {
  int first, last;
  float outPixelInfluenceLowerBound = outPixelCenter - outFilterRadius;
  float outPixelInfluenceUpperBound = outPixelCenter + outFilterRadius;

  float inPixelInfluenceLowerBound = (outPixelInfluenceLowerBound + outShift) * invScale;
  float inPixelInfluenceUpperBound = (outPixelInfluenceUpperBound + outShift) * invScale;

  first = (int)(floorf(inPixelInfluenceLowerBound + 0.5f));
  last = (int)(floorf(inPixelInfluenceUpperBound - 0.5f));
  if (last < first)
    last = first;  // point sample mode can span a value *right* at 0.5, and cause these to cross

  *firstPixel = first;
  *lastPixel = last;
}

static void CalculateOutPixelRange(int* firstPixel, int* lastPixel, float inPixelCenter,
                                   float inPixelsRadius, float scale, float outShift, int outSize) {
  float inPixelInfluenceLowerBound = inPixelCenter - inPixelsRadius;
  float inPixelInfluenceUpperBound = inPixelCenter + inPixelsRadius;
  float outPixelInfluenceLowerBound = inPixelInfluenceLowerBound * scale - outShift;
  float outPixelInfluenceUpperBound = inPixelInfluenceUpperBound * scale - outShift;
  int outFirstPixel = (int)(floorf(outPixelInfluenceLowerBound + 0.5f));
  int outLastPixel = (int)(floorf(outPixelInfluenceUpperBound - 0.5f));

  if (outFirstPixel < 0) outFirstPixel = 0;
  if (outLastPixel >= outSize) outLastPixel = outSize - 1;
  *firstPixel = outFirstPixel;
  *lastPixel = outLastPixel;
}

#define TGFX_MERGE_RUNS_PIXEL_THRESHOLD 16

static void GetConservativeExtents(Sampler* samp, Contributors* range) {
  float scale = samp->scaleInfo.scale;
  float outShift = samp->scaleInfo.pixelShift;
  int inputFullSize = samp->scaleInfo.inputFullSize;
  float invScale = samp->scaleInfo.invScale;

  assert(samp->isGather != 0);

  if (samp->isGather == 1) {
    int inFirstPixel, inLastPixel;
    float outFilterRadius = SupportTrapezoid(invScale) * scale;

    CalculateInPixelRange(&inFirstPixel, &inLastPixel, 0.5, outFilterRadius, invScale, outShift);
    range->n0 = inFirstPixel;
    CalculateInPixelRange(&inFirstPixel, &inLastPixel,
                          ((float)(samp->scaleInfo.outputSubSize - 1)) + 0.5f, outFilterRadius,
                          invScale, outShift);
    range->n1 = inLastPixel;
  } else if (samp->isGather == 2)  // downsample gather, refine
  {
    float inPixelsRadius = SupportTrapezoid(scale) * invScale;
    int filterPixelMargin = samp->filterPixelMargin;
    int outputSubSize = samp->scaleInfo.outputSubSize;
    int inputEnd;
    int n;
    int inFirstPixel, inLastPixel;

    // get a conservative area of the input range
    CalculateInPixelRange(&inFirstPixel, &inLastPixel, 0, 0, invScale, outShift);
    range->n0 = inFirstPixel;
    CalculateInPixelRange(&inFirstPixel, &inLastPixel, (float)outputSubSize, 0, invScale, outShift);
    range->n1 = inLastPixel;

    // now go through the margin to the start of area to find bottom
    n = range->n0 + 1;
    inputEnd = -filterPixelMargin;
    while (n >= inputEnd) {
      int outFirstPixel, outLastPixel;
      CalculateOutPixelRange(&outFirstPixel, &outLastPixel, ((float)n) + 0.5f, inPixelsRadius,
                             scale, outShift, outputSubSize);
      if (outFirstPixel > outLastPixel) break;

      if ((outFirstPixel < outputSubSize) || (outLastPixel >= 0)) range->n0 = n;
      --n;
    }

    // now go through the end of the area through the margin to find top
    n = range->n1 - 1;
    inputEnd = n + 1 + filterPixelMargin;
    while (n <= inputEnd) {
      int outFirstPixel, outLastPixel;
      CalculateOutPixelRange(&outFirstPixel, &outLastPixel, ((float)n) + 0.5f, inPixelsRadius,
                             scale, outShift, outputSubSize);
      if (outFirstPixel > outLastPixel) break;
      if ((outFirstPixel < outputSubSize) || (outLastPixel >= 0)) range->n1 = n;
      ++n;
    }
  }
  // for non-edge-wrap modes, we never read over the edge, so clamp
  if (range->n0 < 0) range->n0 = 0;
  if (range->n1 >= inputFullSize) range->n1 = inputFullSize - 1;
}

static int GetMaxSplit(int splits, int height) {
  int i;
  int max = 0;

  for (i = 0; i < splits; i++) {
    int each = height / (splits - i);
    if (each > max) max = each;
    height -= each;
  }
  return max;
}

// structure that allow us to query and override info for training the costs
struct VFirstInfo {
  double vCost, hCost;
  int controlVFirst;  // 0 = no control, 1 = force hori, 2 = force vert
  int vFirst;
  int vResizeClassification;
  int isGather;
};

#define TGFX_RESIZE_CLASSIFICATIONS 8
static int ShouldDoVerticalFirst(float weightTable[TGFX_RESIZE_CLASSIFICATIONS][4],
                                 int horizontalFilterPixelWidth, float horizontalScale,
                                 int horizontalOutputSize, int verticalFilterPixelWidth,
                                 float verticalScale, int verticalOuputSize, int isGather) {
  double vCost, hCost;
  float* weights;
  int verticalFirst;
  int v_classification;

  // categorize the resize into buckets
  if ((verticalOuputSize <= 4) || (horizontalOutputSize <= 4))
    v_classification = (verticalOuputSize < horizontalOutputSize) ? 6 : 7;
  else if (verticalScale <= 1.0f)
    v_classification = (isGather) ? 1 : 0;
  else if (verticalScale <= 2.0f)
    v_classification = 2;
  else if (verticalScale <= 3.0f)
    v_classification = 3;
  else if (verticalScale <= 4.0f)
    v_classification = 5;
  else
    v_classification = 6;

  // use the right weights
  weights = weightTable[v_classification];

  // this is the costs when you don't take into account modern CPUs with high ipc and simd and caches - wish we had a better estimate
  hCost = (float)horizontalFilterPixelWidth * weights[0] +
          horizontalScale * (float)verticalFilterPixelWidth * weights[1];
  vCost = (float)verticalFilterPixelWidth * weights[2] +
          verticalScale * (float)horizontalFilterPixelWidth * weights[3];

  // use computation estimate to decide vertical first or not
  verticalFirst = (vCost <= hCost) ? 1 : 0;

  return verticalFirst;
}

static float ComputeWeights[5][TGFX_RESIZE_CLASSIFICATIONS]
                           [4] =  // 5 = 0=1chan, 1=2chan, 2=3chan, 3=4chan, 4=7chan
    {{
         {1.00000f, 1.00000f, 0.31250f, 1.00000f},
         {0.56250f, 0.59375f, 0.00000f, 0.96875f},
         {1.00000f, 0.06250f, 0.00000f, 1.00000f},
         {0.00000f, 0.09375f, 1.00000f, 1.00000f},
         {1.00000f, 1.00000f, 1.00000f, 1.00000f},
         {0.03125f, 0.12500f, 1.00000f, 1.00000f},
         {0.06250f, 0.12500f, 0.00000f, 1.00000f},
         {0.00000f, 1.00000f, 0.00000f, 0.03125f},
     },
     {
         {0.00000f, 0.84375f, 0.00000f, 0.03125f},
         {0.09375f, 0.93750f, 0.00000f, 0.78125f},
         {0.87500f, 0.21875f, 0.00000f, 0.96875f},
         {0.09375f, 0.09375f, 1.00000f, 1.00000f},
         {1.00000f, 1.00000f, 1.00000f, 1.00000f},
         {0.03125f, 0.12500f, 1.00000f, 1.00000f},
         {0.06250f, 0.12500f, 0.00000f, 1.00000f},
         {0.00000f, 1.00000f, 0.00000f, 0.53125f},
     },
     {
         {0.00000f, 0.53125f, 0.00000f, 0.03125f},
         {0.06250f, 0.96875f, 0.00000f, 0.53125f},
         {0.87500f, 0.18750f, 0.00000f, 0.93750f},
         {0.00000f, 0.09375f, 1.00000f, 1.00000f},
         {1.00000f, 1.00000f, 1.00000f, 1.00000f},
         {0.03125f, 0.12500f, 1.00000f, 1.00000f},
         {0.06250f, 0.12500f, 0.00000f, 1.00000f},
         {0.00000f, 1.00000f, 0.00000f, 0.56250f},
     },
     {
         {0.00000f, 0.50000f, 0.00000f, 0.71875f},
         {0.06250f, 0.84375f, 0.00000f, 0.87500f},
         {1.00000f, 0.50000f, 0.50000f, 0.96875f},
         {1.00000f, 0.09375f, 0.31250f, 0.50000f},
         {1.00000f, 1.00000f, 1.00000f, 1.00000f},
         {1.00000f, 0.03125f, 0.03125f, 0.53125f},
         {0.18750f, 0.12500f, 0.00000f, 1.00000f},
         {0.00000f, 1.00000f, 0.03125f, 0.18750f},
     },
     {
         {0.00000f, 0.59375f, 0.00000f, 0.96875f},
         {0.06250f, 0.81250f, 0.06250f, 0.59375f},
         {0.75000f, 0.43750f, 0.12500f, 0.96875f},
         {0.87500f, 0.06250f, 0.18750f, 0.43750f},
         {1.00000f, 1.00000f, 1.00000f, 1.00000f},
         {0.15625f, 0.12500f, 1.00000f, 1.00000f},
         {0.06250f, 0.12500f, 0.00000f, 1.00000f},
         {0.00000f, 1.00000f, 0.03125f, 0.34375f},
     }};

// restrict pointers for the output pointers, other loop and unroll control
#if defined(_MSC_VER) && !defined(__clang__)
#define TGFX_STREAMOUT_PTR(star) star __restrict
#define TGFX_NO_UNROLL(ptr) __assume(ptr)  // this oddly keeps msvc from unrolling a loop
#if _MSC_VER >= 1900
#define TGFX_NO_UNROLL_LOOP_START __pragma(loop(no_vector))
#else
#define TGFX_NO_UNROLL_LOOP_START
#endif
#elif defined(__clang__)
#define TGFX_STREAMOUT_PTR(star) star __restrict__
#define TGFX_NO_UNROLL(ptr) __asm__("" ::"r"(ptr))
#if (__clang_major__ >= 4) || ((__clang_major__ >= 3) && (__clang_minor__ >= 5))
#define TGFX_NO_UNROLL_LOOP_START \
  _Pragma("clang loop unroll(disable)") _Pragma("clang loop vectorize(disable)")
#else
#define TGFX_NO_UNROLL_LOOP_START
#endif
#elif defined(__GNUC__)
#define TGFX_STREAMOUT_PTR(star) star __restrict__
#define TGFX_NO_UNROLL(ptr) __asm__("" ::"r"(ptr))
#if __GNUC__ >= 14
#define TGFX_NO_UNROLL_LOOP_START _Pragma("GCC unroll 0") _Pragma("GCC novector")
#else
#define TGFX_NO_UNROLL_LOOP_START
#endif
#define TGFX_NO_UNROLL_LOOP_START_INF_FOR
#else
#define TGFX_STREAMOUT_PTR(star) star
#define TGFX_NO_UNROLL(ptr)
#define TGFX_NO_UNROLL_LOOP_START
#endif

#define TGFX_SIMD_STREAMOUT_PTR(star) TGFX_STREAMOUT_PTR(star)
#define TGFX_SIMD_NO_UNROLL(ptr)
#define TGFX_SIMD_NO_UNROLL_LOOP_START
#define TGFX_SIMD_NO_UNROLL_LOOP_START_INF_FOR

// fancy alpha means we expand to keep both premultipied and non-premultiplied color channels
static void FancyAlphaWeight4ch(float* outBuffer, int widthTimesChannels) {
  float TGFX_STREAMOUT_PTR(*) out = outBuffer;
  // decode buffer aligned to end of outBuffer
  float const* endDecode = outBuffer + (widthTimesChannels / 4) * 7;
  float TGFX_STREAMOUT_PTR(*) decode = (float*)endDecode - widthTimesChannels;

  while (decode < endDecode) {
    float r = decode[0], g = decode[1], b = decode[2], alpha = decode[3];
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = alpha;
    out[4] = r * alpha;
    out[5] = g * alpha;
    out[6] = b * alpha;
    out += 7;
    decode += 4;
  }
}

static void FancyAlphaWeight2ch(float* outBuffer, int widthTimesChannels) {
  float TGFX_STREAMOUT_PTR(*) out = outBuffer;
  float const* endDecode = outBuffer + (widthTimesChannels / 2) * 3;
  float TGFX_STREAMOUT_PTR(*) decode = (float*)endDecode - widthTimesChannels;

  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (decode < endDecode) {
    float x = decode[0], y = decode[1];
    TGFX_SIMD_NO_UNROLL(decode);
    out[0] = x;
    out[1] = y;
    out[2] = x * y;
    out += 3;
    decode += 2;
  }
}

static void FancyAlphaUnweight4ch(float* encodeBuffer, int widthTimesChannels) {
  float TGFX_SIMD_STREAMOUT_PTR(*) encode = encodeBuffer;
  float TGFX_SIMD_STREAMOUT_PTR(*) input = encodeBuffer;
  float const* endOutput = encodeBuffer + widthTimesChannels;

  // fancy RGBA is stored internally as R G B A Rpm Gpm Bpm

  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float alpha = input[3];
    if (alpha < SMALL_FLOAT) {
      encode[0] = input[0];
      encode[1] = input[1];
      encode[2] = input[2];
    } else {
      float ialpha = 1.0f / alpha;
      encode[0] = input[4] * ialpha;
      encode[1] = input[5] * ialpha;
      encode[2] = input[6] * ialpha;
    }
    encode[3] = alpha;

    input += 7;
    encode += 4;
  } while (encode < endOutput);
}

//  format: [X A Xpm][X A Xpm] etc
static void FancyAlphaUnweight2ch(float* encodeBuffer, int widthTimesChannels) {
  float TGFX_SIMD_STREAMOUT_PTR(*) encode = encodeBuffer;
  float TGFX_SIMD_STREAMOUT_PTR(*) input = encodeBuffer;
  float const* endOutput = encodeBuffer + widthTimesChannels;

  do {
    float alpha = input[1];
    encode[0] = input[0];
    if (alpha >= SMALL_FLOAT) encode[0] = input[2] / alpha;
    encode[1] = alpha;

    input += 3;
    encode += 2;
  } while (encode < endOutput);
}

static void SimpleAlphaWeight4ch(float* decodeBuffer, int widthTimesChannels) {
  float TGFX_STREAMOUT_PTR(*) decode = decodeBuffer;
  float const* endDecode = decodeBuffer + widthTimesChannels;

  while (decode < endDecode) {
    float alpha = decode[3];
    decode[0] *= alpha;
    decode[1] *= alpha;
    decode[2] *= alpha;
    decode += 4;
  }
}

static void SimpleAlphaWeight2ch(float* decodeBuffer, int widthTimesChannels) {
  float TGFX_STREAMOUT_PTR(*) decode = decodeBuffer;
  float const* endDecode = decodeBuffer + widthTimesChannels;

  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (decode < endDecode) {
    float alpha = decode[1];
    TGFX_SIMD_NO_UNROLL(decode);
    decode[0] *= alpha;
    decode += 2;
  }
}

static void SimpleAlphaUnweight4ch(float* encodeBuffer, int widthTimesChannels) {
  float TGFX_SIMD_STREAMOUT_PTR(*) encode = encodeBuffer;
  float const* endOutput = encodeBuffer + widthTimesChannels;

  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float alpha = encode[3];

    if (alpha >= SMALL_FLOAT) {
      float ialpha = 1.0f / alpha;
      encode[0] *= ialpha;
      encode[1] *= ialpha;
      encode[2] *= ialpha;
    }
    encode += 4;
  } while (encode < endOutput);
}

static void SimpleAlphaUnweight2ch(float* encodeBuffer, int widthTimesChannels) {
  float TGFX_SIMD_STREAMOUT_PTR(*) encode = encodeBuffer;
  float const* endOutput = encodeBuffer + widthTimesChannels;

  do {
    float alpha = encode[1];
    if (alpha >= SMALL_FLOAT) encode[0] /= alpha;
    encode += 2;
  } while (encode < endOutput);
}

// only used in RGB->BGR or BGR->RGB
static void SimpleFlip3ch(float* decodeBuffer, int widthTimesChannels) {
  float TGFX_STREAMOUT_PTR(*) decode = decodeBuffer;
  float const* endDecode = decodeBuffer + widthTimesChannels;

  endDecode -= 12;
  TGFX_NO_UNROLL_LOOP_START
  while (decode <= endDecode) {
    // 16 instructions
    float t0, t1, t2, t3;
    TGFX_NO_UNROLL(decode);
    t0 = decode[0];
    t1 = decode[3];
    t2 = decode[6];
    t3 = decode[9];
    decode[0] = decode[2];
    decode[3] = decode[5];
    decode[6] = decode[8];
    decode[9] = decode[11];
    decode[2] = t0;
    decode[5] = t1;
    decode[8] = t2;
    decode[11] = t3;
    decode += 12;
  }
  endDecode += 12;

  TGFX_NO_UNROLL_LOOP_START
  while (decode < endDecode) {
    float t = decode[0];
    TGFX_NO_UNROLL(decode);
    decode[0] = decode[2];
    decode[2] = t;
    decode += 3;
  }
}

static void CalculateCoefficientsForGatherUpsample(float outFilterRadius, ScaleInfo* scaleInfo,
                                                   int numContibutors, Contributors* contributors,
                                                   float* coefficientGroup, int coefficientWidth) {
  int n, end;
  float invScale = scaleInfo->invScale;
  float outShift = scaleInfo->pixelShift;
  int numerator = (int)scaleInfo->scaleNumerator;
  int polyphase = ((scaleInfo->scaleIsRational) && (numerator < numContibutors));

  // Looping through out pixels
  end = numContibutors;
  if (polyphase) end = numerator;
  for (n = 0; n < end; n++) {
    int i;
    int last_non_zero;
    float outPixelCenter = (float)n + 0.5f;
    float in_center_of_out = (outPixelCenter + outShift) * invScale;

    int inFirstPixel, inLastPixel;

    CalculateInPixelRange(&inFirstPixel, &inLastPixel, outPixelCenter, outFilterRadius, invScale,
                          outShift);

    // make sure we never generate a range larger than our precalculated coeff width. this only
    // happens in point sample mode, but it's a good safe thing to do anyway.
    if ((inLastPixel - inFirstPixel + 1) > coefficientWidth)
      inLastPixel = inFirstPixel + coefficientWidth - 1;

    last_non_zero = -1;
    for (i = 0; i <= inLastPixel - inFirstPixel; i++) {
      float inPixelCenter = (float)(i + inFirstPixel) + 0.5f;
      float coeff = FilterTrapezoid(in_center_of_out - inPixelCenter, invScale);

      // kill denormals
      if (((coeff < SMALL_FLOAT) && (coeff > -SMALL_FLOAT))) {
        if (i == 0)  // if we're at the front, just eat zero contributors
        {
          assert((inLastPixel - inFirstPixel) != 0);  // there should be at least one contrib
          ++inFirstPixel;
          i--;
          continue;
        }
        coeff = 0;  // make sure is fully zero (should keep denormals away)
      } else
        last_non_zero = i;

      coefficientGroup[i] = coeff;
    }

    inLastPixel = last_non_zero + inFirstPixel;  // kills trailing zeros
    contributors->n0 = inFirstPixel;
    contributors->n1 = inLastPixel;

    assert(contributors->n1 >= contributors->n0);

    ++contributors;
    coefficientGroup += coefficientWidth;
  }
}

// memcpy that is specifically intentionally overlapping (src is smaller then dest, so can be
//   a normal forward copy, bytes is divisible by 4 and bytes is greater than or equal to
//   the diff between dest and src)
static void OverlappingMemcpy(void* dest, void const* src, size_t bytes) {
  char TGFX_SIMD_STREAMOUT_PTR(*) sd = (char*)src;
  char TGFX_SIMD_STREAMOUT_PTR(*) s_end = ((char*)src) + bytes;
  ptrdiff_t ofs_to_dest = (char*)dest - (char*)src;

  if (ofs_to_dest >= 8)  // is the overlap more than 8 away?
  {
    char TGFX_SIMD_STREAMOUT_PTR(*) s_end8 = ((char*)src) + (bytes & (size_t)~7);
    TGFX_NO_UNROLL_LOOP_START
    do {
      TGFX_NO_UNROLL(sd);
      *(uint64_t*)(sd + ofs_to_dest) = *(uint64_t*)sd;
      sd += 8;
    } while (sd < s_end8);

    if (sd == s_end) return;
  }

  TGFX_NO_UNROLL_LOOP_START
  do {
    TGFX_NO_UNROLL(sd);
    *(int*)(sd + ofs_to_dest) = *(int*)sd;
    sd += 4;
  } while (sd < s_end);
}

static void InsertCoeff(Contributors* contribs, float* coeffs, int newPixel, float newCoeff,
                        int maxWidth) {
  if (newPixel <= contribs->n1)  // before the end
  {
    if (newPixel < contribs->n0)  // before the front?
    {
      if ((contribs->n1 - newPixel + 1) <= maxWidth) {
        int j, o = contribs->n0 - newPixel;
        for (j = contribs->n1 - contribs->n0; j <= 0; j--) coeffs[j + o] = coeffs[j];
        for (j = 1; j < o; j--) coeffs[j] = coeffs[0];
        coeffs[0] = newCoeff;
        contribs->n0 = newPixel;
      }
    } else {
      coeffs[newPixel - contribs->n0] += newCoeff;
    }
  } else {
    if ((newPixel - contribs->n0 + 1) <= maxWidth) {
      int j, e = newPixel - contribs->n0;
      for (j = (contribs->n1 - contribs->n0) + 1; j < e;
           j++)  // clear in-betweens coeffs if there are any
        coeffs[j] = 0;

      coeffs[e] = newCoeff;
      contribs->n1 = newPixel;
    }
  }
}
static int EdgeClampFull(int n, int max) {
  if (n < 0) return 0;

  if (n >= max) return max - 1;

  return n;  // NOTREACHED
}

static void ClearupGatheredCoefficients(FilterExtentInfo* filterInfo, ScaleInfo* scaleInfo,
                                        int numContibutors, Contributors* contributors,
                                        float* coefficientGroup, int coefficientWidth) {
  int inputSize = scaleInfo->inputFullSize;
  int inputLastN1 = inputSize - 1;
  int n, end;
  int lowest = 0x7fffffff;
  int highest = -0x7fffffff;
  int widest = -1;
  int numerator = (int)scaleInfo->scaleNumerator;
  int denominator = (int)scaleInfo->scaleDenominator;
  int polyphase = ((scaleInfo->scaleIsRational) && (numerator < numContibutors));
  float* coeffs;
  Contributors* contribs;

  // weight all the coeffs for each sample
  coeffs = coefficientGroup;
  contribs = contributors;
  end = numContibutors;
  if (polyphase) end = numerator;
  for (n = 0; n < end; n++) {
    int i;
    double filterScale, totalFilter = 0;
    int e;

    // add all contribs
    e = contribs->n1 - contribs->n0;
    for (i = 0; i <= e; i++) {
      totalFilter += (double)coeffs[i];
      assert((coeffs[i] >= -2.0f) && (coeffs[i] <= 2.0f));  // check for wonky weights
    }

    // rescale
    if ((totalFilter < SMALL_FLOAT) && (totalFilter > -SMALL_FLOAT)) {
      // all coeffs are extremely small, just zero it
      contribs->n1 = contribs->n0;
      coeffs[0] = 0.0f;
    } else {
      // if the total isn't 1.0, rescale everything
      if ((totalFilter < (1.0f - SMALL_FLOAT)) || (totalFilter > (1.0f + SMALL_FLOAT))) {
        filterScale = ((double)1.0) / totalFilter;

        // scale them all
        for (i = 0; i <= e; i++) coeffs[i] = (float)(coeffs[i] * filterScale);
      }
    }
    ++contribs;
    coeffs += coefficientWidth;
  }

  // if we have a rational for the scale, we can exploit the polyphaseness to not calculate
  //   most of the coefficients, so we copy them here
  if (polyphase) {
    Contributors* prevContribs = contributors;
    Contributors* curContribs = contributors + numerator;

    for (n = numerator; n < numContibutors; n++) {
      curContribs->n0 = prevContribs->n0 + denominator;
      curContribs->n1 = prevContribs->n1 + denominator;
      ++curContribs;
      ++prevContribs;
    }
    OverlappingMemcpy(
        coefficientGroup + numerator * coefficientWidth, coefficientGroup,
        (size_t)(numContibutors - numerator) * (size_t)coefficientWidth * sizeof(coeffs[0]));
  }

  coeffs = coefficientGroup;
  contribs = contributors;

  for (n = 0; n < numContibutors; n++) {
    int i;

    // for clamp, calculate the true inbounds position and just add that to the existing weight

    // right hand side first
    if (contribs->n1 > inputLastN1) {
      int start = contribs->n0;
      int endi = contribs->n1;
      contribs->n1 = inputLastN1;
      for (i = inputSize; i <= endi; i++)
        InsertCoeff(contribs, coeffs, EdgeClampFull(i, inputSize), coeffs[i - start],
                    coefficientWidth);
    }

    // now check left hand edge
    if (contribs->n0 < 0) {
      int saveN0;
      float saveN0coeff;
      float* c = coeffs - (contribs->n0 + 1);

      // reinsert the coeffs with it clamped (insert accumulates, if the coeffs exist)
      for (i = -1; i > contribs->n0; i--)
        InsertCoeff(contribs, coeffs, EdgeClampFull(i, inputSize), *c--, coefficientWidth);
      saveN0 = contribs->n0;
      saveN0coeff = c
          [0];  // save it, since we didn't do the final one (i==n0), because there might be too many coeffs to hold (before we resize)!

      // now slide all the coeffs down (since we have accumulated them in the positive contribs) and reset the first contrib
      contribs->n0 = 0;
      for (i = 0; i <= contribs->n1; i++) coeffs[i] = coeffs[i - saveN0];

      // now that we have shrunk down the contribs, we insert the first one safely
      InsertCoeff(contribs, coeffs, EdgeClampFull(saveN0, inputSize), saveN0coeff,
                  coefficientWidth);
    }

    if (contribs->n0 <= contribs->n1) {
      int diff = contribs->n1 - contribs->n0 + 1;
      while (diff && (coeffs[diff - 1] == 0.0f)) --diff;

      contribs->n1 = contribs->n0 + diff - 1;

      if (contribs->n0 <= contribs->n1) {
        if (contribs->n0 < lowest) lowest = contribs->n0;
        if (contribs->n1 > highest) highest = contribs->n1;
        if (diff > widest) widest = diff;
      }

      // re-zero out unused coefficients (if any)
      for (i = diff; i < coefficientWidth; i++) coeffs[i] = 0.0f;
    }

    ++contribs;
    coeffs += coefficientWidth;
  }
  filterInfo->lowest = lowest;
  filterInfo->highest = highest;
  filterInfo->widest = widest;
}

static void CalculateCoefficientsForGatherDownsample(int start, int end, float inPixelsRadius,
                                                     ScaleInfo* scaleInfo, int coefficientWidth,
                                                     Contributors* contributors,
                                                     float* coefficientGroup) {
  int inPixel;
  int i;
  int firstOutInited = -1;
  float scale = scaleInfo->scale;
  float outShift = scaleInfo->pixelShift;
  int outSize = scaleInfo->outputSubSize;
  int numerator = (int)scaleInfo->scaleNumerator;
  int polyphase = ((scaleInfo->scaleIsRational) && (numerator < outSize));

  // Loop through the input pixels
  for (inPixel = start; inPixel < end; inPixel++) {
    float inPixelCenter = (float)inPixel + 0.5f;
    float outCenterOfIn = inPixelCenter * scale - outShift;
    int outFirstPixel, outLastPixel;

    CalculateOutPixelRange(&outFirstPixel, &outLastPixel, inPixelCenter, inPixelsRadius, scale,
                           outShift, outSize);

    if (outFirstPixel > outLastPixel) continue;

    // clamp or exit if we are using polyphase filtering, and the limit is up
    if (polyphase) {
      // when polyphase, you only have to do coeffs up to the numerator count
      if (outFirstPixel == numerator) break;

      // don't do any extra work, clamp last pixel at numerator too
      if (outLastPixel >= numerator) outLastPixel = numerator - 1;
    }

    for (i = 0; i <= outLastPixel - outFirstPixel; i++) {
      float outPixelCenter = (float)(i + outFirstPixel) + 0.5f;
      float x = outPixelCenter - outCenterOfIn;
      float coeff = FilterTrapezoid(x, scale) * scale;

      // kill the coeff if it's too small (avoid denormals)
      if (((coeff < SMALL_FLOAT) && (coeff > -SMALL_FLOAT))) coeff = 0.0f;

      {
        int out = i + outFirstPixel;
        float* coeffs = coefficientGroup + out * coefficientWidth;
        Contributors* contribs = contributors + out;

        // is this the first time this output pixel has been seen?  Init it.
        if (out > firstOutInited) {
          assert(out == (firstOutInited + 1));  // ensure we have only advanced one at time
          firstOutInited = out;
          contribs->n0 = inPixel;
          contribs->n1 = inPixel;
          coeffs[0] = coeff;
        } else {
          // insert on end (always in order)
          if (coeffs[0] == 0.0f)  // if the first coefficent is zero, then zap it for this coeffs
          {
            assert((inPixel - contribs->n0) == 1);  // ensure that when we zap, we're at the 2nd pos
            contribs->n0 = inPixel;
          }
          contribs->n1 = inPixel;
          assert((inPixel - contribs->n0) < coefficientWidth);
          coeffs[inPixel - contribs->n0] = coeff;
        }
      }
    }
  }
}

static void CalculateFilters(Sampler* samp, Sampler* otherAxisForPivot) {
  int n;
  float scale = samp->scaleInfo.scale;
  float invScale = samp->scaleInfo.invScale;
  int inputFullSize = samp->scaleInfo.inputFullSize;
  int gatherNumContributors = samp->numContibutors;
  Contributors* gatherContributors = samp->contributors;
  float* gatherCoeffs = samp->coefficients;
  int gatherCoefficientWidth = samp->coefficientWidth;

  switch (samp->isGather) {
    case 1:  // gather upsample
    {
      float outPixelsRadius = SupportTrapezoid(invScale) * scale;

      CalculateCoefficientsForGatherUpsample(outPixelsRadius, &samp->scaleInfo,
                                             gatherNumContributors, gatherContributors,
                                             gatherCoeffs, gatherCoefficientWidth);

      ClearupGatheredCoefficients(&samp->extentInfo, &samp->scaleInfo, gatherNumContributors,
                                  gatherContributors, gatherCoeffs, gatherCoefficientWidth);
    } break;

    case 0:  // scatter downsample (only on vertical)
    case 2:  // gather downsample
    {
      float inPixelsRadius = SupportTrapezoid(scale) * invScale;
      int filterPixelMargin = samp->filterPixelMargin;
      int inputEnd = inputFullSize + filterPixelMargin;

      // if this is a scatter, we do a downsample gather to get the coeffs, and then pivot after
      if (!samp->isGather) {
        // check if we are using the same gather downsample on the horizontal as this vertical,
        //   if so, then we don't have to generate them, we can just pivot from the horizontal.
        if (otherAxisForPivot) {
          gatherContributors = otherAxisForPivot->contributors;
          gatherCoeffs = otherAxisForPivot->coefficients;
          gatherCoefficientWidth = otherAxisForPivot->coefficientWidth;
          gatherNumContributors = otherAxisForPivot->numContibutors;
          samp->extentInfo.lowest = otherAxisForPivot->extentInfo.lowest;
          samp->extentInfo.highest = otherAxisForPivot->extentInfo.highest;
          samp->extentInfo.widest = otherAxisForPivot->extentInfo.widest;
          goto jumpRightToPivot;
        }

        gatherContributors = samp->gatherPrescatterContibutors;
        gatherCoeffs = samp->gatherPrescatterCoefficients;
        gatherCoefficientWidth = samp->gatherPrescatterCoefficientWidth;
        gatherNumContributors = samp->gatherPrescatterNumContributors;
      }

      CalculateCoefficientsForGatherDownsample(-filterPixelMargin, inputEnd, inPixelsRadius,
                                               &samp->scaleInfo, gatherCoefficientWidth,
                                               gatherContributors, gatherCoeffs);

      ClearupGatheredCoefficients(&samp->extentInfo, &samp->scaleInfo, gatherNumContributors,
                                  gatherContributors, gatherCoeffs, gatherCoefficientWidth);

      if (!samp->isGather) {
        // if this is a scatter (vertical only), then we need to pivot the coeffs
        Contributors* scatterContributors;
        int highestSet;

      jumpRightToPivot:

        highestSet = (-filterPixelMargin) - 1;
        for (n = 0; n < gatherNumContributors; n++) {
          int k;
          int gn0 = gatherContributors->n0, gn1 = gatherContributors->n1;
          int scatterCoefficientWidth = samp->coefficientWidth;
          float* scatterCoeffs =
              samp->coefficients + (gn0 + filterPixelMargin) * scatterCoefficientWidth;
          float* gCoeffs = gatherCoeffs;
          scatterContributors = samp->contributors + (gn0 + filterPixelMargin);

          for (k = gn0; k <= gn1; k++) {
            float gc = *gCoeffs++;

            // skip zero and denormals - must skip zeros to avoid adding coeffs beyond scatterCoefficientWidth
            //   (which happens when pivoting from horizontal, which might have dummy zeros)
            if (((gc >= SMALL_FLOAT) || (gc <= -SMALL_FLOAT))) {
              if ((k > highestSet) || (scatterContributors->n0 > scatterContributors->n1)) {
                {
                  // if we are skipping over several contributors, we need to clear the skipped ones
                  Contributors* clearContributors =
                      samp->contributors + (highestSet + filterPixelMargin + 1);
                  while (clearContributors < scatterContributors) {
                    clearContributors->n0 = 0;
                    clearContributors->n1 = -1;
                    ++clearContributors;
                  }
                }
                scatterContributors->n0 = n;
                scatterContributors->n1 = n;
                scatterCoeffs[0] = gc;
                highestSet = k;
              } else {
                InsertCoeff(scatterContributors, scatterCoeffs, n, gc, scatterCoefficientWidth);
              }
              assert((scatterContributors->n1 - scatterContributors->n0 + 1) <=
                     scatterCoefficientWidth);
            }
            ++scatterContributors;
            scatterCoeffs += scatterCoefficientWidth;
          }

          ++gatherContributors;
          gatherCoeffs += gatherCoefficientWidth;
        }

        // now clear any unset contribs
        {
          Contributors* clearContributors =
              samp->contributors + (highestSet + filterPixelMargin + 1);
          Contributors* endContributors = samp->contributors + samp->numContibutors;
          while (clearContributors < endContributors) {
            clearContributors->n0 = 0;
            clearContributors->n1 = -1;
            ++clearContributors;
          }
        }
      }
    } break;
  }
}

//========================================================================================================
// scanline decoders and encoders
#define TGFX_CODER_MIN_NUM 1
#define TGFX_IMAGE_RESIZE_DO_CODERS
#include SOURCE_FILE

#define TGFX_DECODE_SUFFIX BGRA
#define TGFX_DECODE_SWIZZLE
#define TGFX_DECODE_ORDER0 2
#define TGFX_DECODE_ORDER1 1
#define TGFX_DECODE_ORDER2 0
#define TGFX_DECODE_ORDER3 3
#define TGFX_ENCODE_ORDER0 2
#define TGFX_ENCODE_ORDER1 1
#define TGFX_ENCODE_ORDER2 0
#define TGFX_ENCODE_ORDER3 3
#define TGFX_CODER_MIN_NUM 4
#define TGFX_IMAGE_RESIZE_DO_CODERS
#include SOURCE_FILE

#define TGFX_DECODE_SUFFIX ARGB
#define TGFX_DECODE_SWIZZLE
#define TGFX_DECODE_ORDER0 1
#define TGFX_DECODE_ORDER1 2
#define TGFX_DECODE_ORDER2 3
#define TGFX_DECODE_ORDER3 0
#define TGFX_ENCODE_ORDER0 3
#define TGFX_ENCODE_ORDER1 0
#define TGFX_ENCODE_ORDER2 1
#define TGFX_ENCODE_ORDER3 2
#define TGFX_CODER_MIN_NUM 4
#define TGFX_IMAGE_RESIZE_DO_CODERS
#include SOURCE_FILE

#define TGFX_DECODE_SUFFIX ABGR
#define TGFX_DECODE_SWIZZLE
#define TGFX_DECODE_ORDER0 3
#define TGFX_DECODE_ORDER1 2
#define TGFX_DECODE_ORDER2 1
#define TGFX_DECODE_ORDER3 0
#define TGFX_ENCODE_ORDER0 3
#define TGFX_ENCODE_ORDER1 2
#define TGFX_ENCODE_ORDER2 1
#define TGFX_ENCODE_ORDER3 0
#define TGFX_CODER_MIN_NUM 4
#define TGFX_IMAGE_RESIZE_DO_CODERS
#include SOURCE_FILE

#define TGFX_DECODE_SUFFIX AR
#define TGFX_DECODE_SWIZZLE
#define TGFX_DECODE_ORDER0 1
#define TGFX_DECODE_ORDER1 0
#define TGFX_DECODE_ORDER2 3
#define TGFX_DECODE_ORDER3 2
#define TGFX_ENCODE_ORDER0 1
#define TGFX_ENCODE_ORDER1 0
#define TGFX_ENCODE_ORDER2 3
#define TGFX_ENCODE_ORDER3 2
#define TGFX_CODER_MIN_NUM 2
#define TGFX_IMAGE_RESIZE_DO_CODERS
#include SOURCE_FILE

//=================
// Do 1 channel horizontal routines
#define TGFX_1_COEFF_ONLY() \
  float tot;                \
  tot = decode[0] * hc[0];

#define TGFX_2_COEFF_ONLY() \
  float tot;                \
  tot = decode[0] * hc[0];  \
  tot += decode[1] * hc[1];

#define TGFX_3_COEFF_ONLY() \
  float tot;                \
  tot = decode[0] * hc[0];  \
  tot += decode[1] * hc[1]; \
  tot += decode[2] * hc[2];

#define TGFX_STORE_OUTPUT_TINY()              \
  output[0] = tot;                            \
  horizontalCoefficients += coefficientWidth; \
  ++horizontalContributors;                   \
  output += 1;

#define TGFX_4_COEFF_START()    \
  float tot0, tot1, tot2, tot3; \
  tot0 = decode[0] * hc[0];     \
  tot1 = decode[1] * hc[1];     \
  tot2 = decode[2] * hc[2];     \
  tot3 = decode[3] * hc[3];

#define TGFX_4_COEFF_CONTINUE_FROM_4(ofs)    \
  tot0 += decode[0 + (ofs)] * hc[0 + (ofs)]; \
  tot1 += decode[1 + (ofs)] * hc[1 + (ofs)]; \
  tot2 += decode[2 + (ofs)] * hc[2 + (ofs)]; \
  tot3 += decode[3 + (ofs)] * hc[3 + (ofs)];

#define TGFX_1_COEFF_REMNANT(ofs) tot0 += decode[0 + (ofs)] * hc[0 + (ofs)];

#define TGFX_2_COEFF_REMNANT(ofs)            \
  tot0 += decode[0 + (ofs)] * hc[0 + (ofs)]; \
  tot1 += decode[1 + (ofs)] * hc[1 + (ofs)];

#define TGFX_3_COEFF_REMNANT(ofs)            \
  tot0 += decode[0 + (ofs)] * hc[0 + (ofs)]; \
  tot1 += decode[1 + (ofs)] * hc[1 + (ofs)]; \
  tot2 += decode[2 + (ofs)] * hc[2 + (ofs)];

#define TGFX_STORE_OUTPUT()                   \
  output[0] = (tot0 + tot2) + (tot1 + tot3);  \
  horizontalCoefficients += coefficientWidth; \
  ++horizontalContributors;                   \
  output += 1;

#define TGFX_HORIZONTAL_CHANNELS 1
#define TGFX_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCE_FILE

//=================
// Do 2 channel horizontal routines

#define TGFX_1_COEFF_ONLY() \
  float tota, totb, c;      \
  c = hc[0];                \
  tota = decode[0] * c;     \
  totb = decode[1] * c;

#define TGFX_2_COEFF_ONLY() \
  float tota, totb, c;      \
  c = hc[0];                \
  tota = decode[0] * c;     \
  totb = decode[1] * c;     \
  c = hc[1];                \
  tota += decode[2] * c;    \
  totb += decode[3] * c;

// this weird order of add matches the simd
#define TGFX_3_COEFF_ONLY() \
  float tota, totb, c;      \
  c = hc[0];                \
  tota = decode[0] * c;     \
  totb = decode[1] * c;     \
  c = hc[2];                \
  tota += decode[4] * c;    \
  totb += decode[5] * c;    \
  c = hc[1];                \
  tota += decode[2] * c;    \
  totb += decode[3] * c;

#define TGFX_STORE_OUTPUT_TINY()              \
  output[0] = tota;                           \
  output[1] = totb;                           \
  horizontalCoefficients += coefficientWidth; \
  ++horizontalContributors;                   \
  output += 2;

#define TGFX_4_COEFF_START()                                       \
  float tota0, tota1, tota2, tota3, totb0, totb1, totb2, totb3, c; \
  c = hc[0];                                                       \
  tota0 = decode[0] * c;                                           \
  totb0 = decode[1] * c;                                           \
  c = hc[1];                                                       \
  tota1 = decode[2] * c;                                           \
  totb1 = decode[3] * c;                                           \
  c = hc[2];                                                       \
  tota2 = decode[4] * c;                                           \
  totb2 = decode[5] * c;                                           \
  c = hc[3];                                                       \
  tota3 = decode[6] * c;                                           \
  totb3 = decode[7] * c;

#define TGFX_4_COEFF_CONTINUE_FROM_4(ofs) \
  c = hc[0 + (ofs)];                      \
  tota0 += decode[0 + (ofs)*2] * c;       \
  totb0 += decode[1 + (ofs)*2] * c;       \
  c = hc[1 + (ofs)];                      \
  tota1 += decode[2 + (ofs)*2] * c;       \
  totb1 += decode[3 + (ofs)*2] * c;       \
  c = hc[2 + (ofs)];                      \
  tota2 += decode[4 + (ofs)*2] * c;       \
  totb2 += decode[5 + (ofs)*2] * c;       \
  c = hc[3 + (ofs)];                      \
  tota3 += decode[6 + (ofs)*2] * c;       \
  totb3 += decode[7 + (ofs)*2] * c;

#define TGFX_1_COEFF_REMNANT(ofs)   \
  c = hc[0 + (ofs)];                \
  tota0 += decode[0 + (ofs)*2] * c; \
  totb0 += decode[1 + (ofs)*2] * c;

#define TGFX_2_COEFF_REMNANT(ofs)   \
  c = hc[0 + (ofs)];                \
  tota0 += decode[0 + (ofs)*2] * c; \
  totb0 += decode[1 + (ofs)*2] * c; \
  c = hc[1 + (ofs)];                \
  tota1 += decode[2 + (ofs)*2] * c; \
  totb1 += decode[3 + (ofs)*2] * c;

#define TGFX_3_COEFF_REMNANT(ofs)   \
  c = hc[0 + (ofs)];                \
  tota0 += decode[0 + (ofs)*2] * c; \
  totb0 += decode[1 + (ofs)*2] * c; \
  c = hc[1 + (ofs)];                \
  tota1 += decode[2 + (ofs)*2] * c; \
  totb1 += decode[3 + (ofs)*2] * c; \
  c = hc[2 + (ofs)];                \
  tota2 += decode[4 + (ofs)*2] * c; \
  totb2 += decode[5 + (ofs)*2] * c;

#define TGFX_STORE_OUTPUT()                      \
  output[0] = (tota0 + tota2) + (tota1 + tota3); \
  output[1] = (totb0 + totb2) + (totb1 + totb3); \
  horizontalCoefficients += coefficientWidth;    \
  ++horizontalContributors;                      \
  output += 2;

#define TGFX_HORIZONTAL_CHANNELS 2
#define TGFX_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCE_FILE

//=================
// Do 3 channel horizontal routines

#define TGFX_1_COEFF_ONLY()  \
  float tot0, tot1, tot2, c; \
  c = hc[0];                 \
  tot0 = decode[0] * c;      \
  tot1 = decode[1] * c;      \
  tot2 = decode[2] * c;

#define TGFX_2_COEFF_ONLY()  \
  float tot0, tot1, tot2, c; \
  c = hc[0];                 \
  tot0 = decode[0] * c;      \
  tot1 = decode[1] * c;      \
  tot2 = decode[2] * c;      \
  c = hc[1];                 \
  tot0 += decode[3] * c;     \
  tot1 += decode[4] * c;     \
  tot2 += decode[5] * c;

#define TGFX_3_COEFF_ONLY()  \
  float tot0, tot1, tot2, c; \
  c = hc[0];                 \
  tot0 = decode[0] * c;      \
  tot1 = decode[1] * c;      \
  tot2 = decode[2] * c;      \
  c = hc[1];                 \
  tot0 += decode[3] * c;     \
  tot1 += decode[4] * c;     \
  tot2 += decode[5] * c;     \
  c = hc[2];                 \
  tot0 += decode[6] * c;     \
  tot1 += decode[7] * c;     \
  tot2 += decode[8] * c;

#define TGFX_STORE_OUTPUT_TINY()              \
  output[0] = tot0;                           \
  output[1] = tot1;                           \
  output[2] = tot2;                           \
  horizontalCoefficients += coefficientWidth; \
  ++horizontalContributors;                   \
  output += 3;

#define TGFX_4_COEFF_START()                                                                   \
  float tota0, tota1, tota2, totb0, totb1, totb2, totc0, totc1, totc2, totd0, totd1, totd2, c; \
  c = hc[0];                                                                                   \
  tota0 = decode[0] * c;                                                                       \
  tota1 = decode[1] * c;                                                                       \
  tota2 = decode[2] * c;                                                                       \
  c = hc[1];                                                                                   \
  totb0 = decode[3] * c;                                                                       \
  totb1 = decode[4] * c;                                                                       \
  totb2 = decode[5] * c;                                                                       \
  c = hc[2];                                                                                   \
  totc0 = decode[6] * c;                                                                       \
  totc1 = decode[7] * c;                                                                       \
  totc2 = decode[8] * c;                                                                       \
  c = hc[3];                                                                                   \
  totd0 = decode[9] * c;                                                                       \
  totd1 = decode[10] * c;                                                                      \
  totd2 = decode[11] * c;

#define TGFX_4_COEFF_CONTINUE_FROM_4(ofs) \
  c = hc[0 + (ofs)];                      \
  tota0 += decode[0 + (ofs)*3] * c;       \
  tota1 += decode[1 + (ofs)*3] * c;       \
  tota2 += decode[2 + (ofs)*3] * c;       \
  c = hc[1 + (ofs)];                      \
  totb0 += decode[3 + (ofs)*3] * c;       \
  totb1 += decode[4 + (ofs)*3] * c;       \
  totb2 += decode[5 + (ofs)*3] * c;       \
  c = hc[2 + (ofs)];                      \
  totc0 += decode[6 + (ofs)*3] * c;       \
  totc1 += decode[7 + (ofs)*3] * c;       \
  totc2 += decode[8 + (ofs)*3] * c;       \
  c = hc[3 + (ofs)];                      \
  totd0 += decode[9 + (ofs)*3] * c;       \
  totd1 += decode[10 + (ofs)*3] * c;      \
  totd2 += decode[11 + (ofs)*3] * c;

#define TGFX_1_COEFF_REMNANT(ofs)   \
  c = hc[0 + (ofs)];                \
  tota0 += decode[0 + (ofs)*3] * c; \
  tota1 += decode[1 + (ofs)*3] * c; \
  tota2 += decode[2 + (ofs)*3] * c;

#define TGFX_2_COEFF_REMNANT(ofs)   \
  c = hc[0 + (ofs)];                \
  tota0 += decode[0 + (ofs)*3] * c; \
  tota1 += decode[1 + (ofs)*3] * c; \
  tota2 += decode[2 + (ofs)*3] * c; \
  c = hc[1 + (ofs)];                \
  totb0 += decode[3 + (ofs)*3] * c; \
  totb1 += decode[4 + (ofs)*3] * c; \
  totb2 += decode[5 + (ofs)*3] * c;

#define TGFX_3_COEFF_REMNANT(ofs)   \
  c = hc[0 + (ofs)];                \
  tota0 += decode[0 + (ofs)*3] * c; \
  tota1 += decode[1 + (ofs)*3] * c; \
  tota2 += decode[2 + (ofs)*3] * c; \
  c = hc[1 + (ofs)];                \
  totb0 += decode[3 + (ofs)*3] * c; \
  totb1 += decode[4 + (ofs)*3] * c; \
  totb2 += decode[5 + (ofs)*3] * c; \
  c = hc[2 + (ofs)];                \
  totc0 += decode[6 + (ofs)*3] * c; \
  totc1 += decode[7 + (ofs)*3] * c; \
  totc2 += decode[8 + (ofs)*3] * c;

#define TGFX_STORE_OUTPUT()                      \
  output[0] = (tota0 + totc0) + (totb0 + totd0); \
  output[1] = (tota1 + totc1) + (totb1 + totd1); \
  output[2] = (tota2 + totc2) + (totb2 + totd2); \
  horizontalCoefficients += coefficientWidth;    \
  ++horizontalContributors;                      \
  output += 3;

#define TGFX_HORIZONTAL_CHANNELS 3
#define TGFX_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCE_FILE

//=================
// Do 4 channel horizontal routines

#define TGFX_1_COEFF_ONLY()    \
  float p0, p1, p2, p3, c;     \
  TGFX_SIMD_NO_UNROLL(decode); \
  c = hc[0];                   \
  p0 = decode[0] * c;          \
  p1 = decode[1] * c;          \
  p2 = decode[2] * c;          \
  p3 = decode[3] * c;

#define TGFX_2_COEFF_ONLY()    \
  float p0, p1, p2, p3, c;     \
  TGFX_SIMD_NO_UNROLL(decode); \
  c = hc[0];                   \
  p0 = decode[0] * c;          \
  p1 = decode[1] * c;          \
  p2 = decode[2] * c;          \
  p3 = decode[3] * c;          \
  c = hc[1];                   \
  p0 += decode[4] * c;         \
  p1 += decode[5] * c;         \
  p2 += decode[6] * c;         \
  p3 += decode[7] * c;

#define TGFX_3_COEFF_ONLY()    \
  float p0, p1, p2, p3, c;     \
  TGFX_SIMD_NO_UNROLL(decode); \
  c = hc[0];                   \
  p0 = decode[0] * c;          \
  p1 = decode[1] * c;          \
  p2 = decode[2] * c;          \
  p3 = decode[3] * c;          \
  c = hc[1];                   \
  p0 += decode[4] * c;         \
  p1 += decode[5] * c;         \
  p2 += decode[6] * c;         \
  p3 += decode[7] * c;         \
  c = hc[2];                   \
  p0 += decode[8] * c;         \
  p1 += decode[9] * c;         \
  p2 += decode[10] * c;        \
  p3 += decode[11] * c;

#define TGFX_STORE_OUTPUT_TINY()              \
  output[0] = p0;                             \
  output[1] = p1;                             \
  output[2] = p2;                             \
  output[3] = p3;                             \
  horizontalCoefficients += coefficientWidth; \
  ++horizontalContributors;                   \
  output += 4;

#define TGFX_4_COEFF_START()               \
  float x0, x1, x2, x3, y0, y1, y2, y3, c; \
  TGFX_SIMD_NO_UNROLL(decode);             \
  c = hc[0];                               \
  x0 = decode[0] * c;                      \
  x1 = decode[1] * c;                      \
  x2 = decode[2] * c;                      \
  x3 = decode[3] * c;                      \
  c = hc[1];                               \
  y0 = decode[4] * c;                      \
  y1 = decode[5] * c;                      \
  y2 = decode[6] * c;                      \
  y3 = decode[7] * c;                      \
  c = hc[2];                               \
  x0 += decode[8] * c;                     \
  x1 += decode[9] * c;                     \
  x2 += decode[10] * c;                    \
  x3 += decode[11] * c;                    \
  c = hc[3];                               \
  y0 += decode[12] * c;                    \
  y1 += decode[13] * c;                    \
  y2 += decode[14] * c;                    \
  y3 += decode[15] * c;

#define TGFX_4_COEFF_CONTINUE_FROM_4(ofs) \
  TGFX_SIMD_NO_UNROLL(decode);            \
  c = hc[0 + (ofs)];                      \
  x0 += decode[0 + (ofs)*4] * c;          \
  x1 += decode[1 + (ofs)*4] * c;          \
  x2 += decode[2 + (ofs)*4] * c;          \
  x3 += decode[3 + (ofs)*4] * c;          \
  c = hc[1 + (ofs)];                      \
  y0 += decode[4 + (ofs)*4] * c;          \
  y1 += decode[5 + (ofs)*4] * c;          \
  y2 += decode[6 + (ofs)*4] * c;          \
  y3 += decode[7 + (ofs)*4] * c;          \
  c = hc[2 + (ofs)];                      \
  x0 += decode[8 + (ofs)*4] * c;          \
  x1 += decode[9 + (ofs)*4] * c;          \
  x2 += decode[10 + (ofs)*4] * c;         \
  x3 += decode[11 + (ofs)*4] * c;         \
  c = hc[3 + (ofs)];                      \
  y0 += decode[12 + (ofs)*4] * c;         \
  y1 += decode[13 + (ofs)*4] * c;         \
  y2 += decode[14 + (ofs)*4] * c;         \
  y3 += decode[15 + (ofs)*4] * c;

#define TGFX_1_COEFF_REMNANT(ofs) \
  TGFX_SIMD_NO_UNROLL(decode);    \
  c = hc[0 + (ofs)];              \
  x0 += decode[0 + (ofs)*4] * c;  \
  x1 += decode[1 + (ofs)*4] * c;  \
  x2 += decode[2 + (ofs)*4] * c;  \
  x3 += decode[3 + (ofs)*4] * c;

#define TGFX_2_COEFF_REMNANT(ofs) \
  TGFX_SIMD_NO_UNROLL(decode);    \
  c = hc[0 + (ofs)];              \
  x0 += decode[0 + (ofs)*4] * c;  \
  x1 += decode[1 + (ofs)*4] * c;  \
  x2 += decode[2 + (ofs)*4] * c;  \
  x3 += decode[3 + (ofs)*4] * c;  \
  c = hc[1 + (ofs)];              \
  y0 += decode[4 + (ofs)*4] * c;  \
  y1 += decode[5 + (ofs)*4] * c;  \
  y2 += decode[6 + (ofs)*4] * c;  \
  y3 += decode[7 + (ofs)*4] * c;

#define TGFX_3_COEFF_REMNANT(ofs) \
  TGFX_SIMD_NO_UNROLL(decode);    \
  c = hc[0 + (ofs)];              \
  x0 += decode[0 + (ofs)*4] * c;  \
  x1 += decode[1 + (ofs)*4] * c;  \
  x2 += decode[2 + (ofs)*4] * c;  \
  x3 += decode[3 + (ofs)*4] * c;  \
  c = hc[1 + (ofs)];              \
  y0 += decode[4 + (ofs)*4] * c;  \
  y1 += decode[5 + (ofs)*4] * c;  \
  y2 += decode[6 + (ofs)*4] * c;  \
  y3 += decode[7 + (ofs)*4] * c;  \
  c = hc[2 + (ofs)];              \
  x0 += decode[8 + (ofs)*4] * c;  \
  x1 += decode[9 + (ofs)*4] * c;  \
  x2 += decode[10 + (ofs)*4] * c; \
  x3 += decode[11 + (ofs)*4] * c;

#define TGFX_STORE_OUTPUT()                   \
  output[0] = x0 + y0;                        \
  output[1] = x1 + y1;                        \
  output[2] = x2 + y2;                        \
  output[3] = x3 + y3;                        \
  horizontalCoefficients += coefficientWidth; \
  ++horizontalContributors;                   \
  output += 4;

#define TGFX_HORIZONTAL_CHANNELS 4
#define TGFX_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCE_FILE

//=================
// Do 7 channel horizontal routines

#define TGFX_1_COEFF_ONLY()                          \
  float tot0, tot1, tot2, tot3, tot4, tot5, tot6, c; \
  c = hc[0];                                         \
  tot0 = decode[0] * c;                              \
  tot1 = decode[1] * c;                              \
  tot2 = decode[2] * c;                              \
  tot3 = decode[3] * c;                              \
  tot4 = decode[4] * c;                              \
  tot5 = decode[5] * c;                              \
  tot6 = decode[6] * c;

#define TGFX_2_COEFF_ONLY()                          \
  float tot0, tot1, tot2, tot3, tot4, tot5, tot6, c; \
  c = hc[0];                                         \
  tot0 = decode[0] * c;                              \
  tot1 = decode[1] * c;                              \
  tot2 = decode[2] * c;                              \
  tot3 = decode[3] * c;                              \
  tot4 = decode[4] * c;                              \
  tot5 = decode[5] * c;                              \
  tot6 = decode[6] * c;                              \
  c = hc[1];                                         \
  tot0 += decode[7] * c;                             \
  tot1 += decode[8] * c;                             \
  tot2 += decode[9] * c;                             \
  tot3 += decode[10] * c;                            \
  tot4 += decode[11] * c;                            \
  tot5 += decode[12] * c;                            \
  tot6 += decode[13] * c;

#define TGFX_3_COEFF_ONLY()                          \
  float tot0, tot1, tot2, tot3, tot4, tot5, tot6, c; \
  c = hc[0];                                         \
  tot0 = decode[0] * c;                              \
  tot1 = decode[1] * c;                              \
  tot2 = decode[2] * c;                              \
  tot3 = decode[3] * c;                              \
  tot4 = decode[4] * c;                              \
  tot5 = decode[5] * c;                              \
  tot6 = decode[6] * c;                              \
  c = hc[1];                                         \
  tot0 += decode[7] * c;                             \
  tot1 += decode[8] * c;                             \
  tot2 += decode[9] * c;                             \
  tot3 += decode[10] * c;                            \
  tot4 += decode[11] * c;                            \
  tot5 += decode[12] * c;                            \
  tot6 += decode[13] * c;                            \
  c = hc[2];                                         \
  tot0 += decode[14] * c;                            \
  tot1 += decode[15] * c;                            \
  tot2 += decode[16] * c;                            \
  tot3 += decode[17] * c;                            \
  tot4 += decode[18] * c;                            \
  tot5 += decode[19] * c;                            \
  tot6 += decode[20] * c;

#define TGFX_STORE_OUTPUT_TINY()              \
  output[0] = tot0;                           \
  output[1] = tot1;                           \
  output[2] = tot2;                           \
  output[3] = tot3;                           \
  output[4] = tot4;                           \
  output[5] = tot5;                           \
  output[6] = tot6;                           \
  horizontalCoefficients += coefficientWidth; \
  ++horizontalContributors;                   \
  output += 7;

#define TGFX_4_COEFF_START()                                       \
  float x0, x1, x2, x3, x4, x5, x6, y0, y1, y2, y3, y4, y5, y6, c; \
  TGFX_SIMD_NO_UNROLL(decode);                                     \
  c = hc[0];                                                       \
  x0 = decode[0] * c;                                              \
  x1 = decode[1] * c;                                              \
  x2 = decode[2] * c;                                              \
  x3 = decode[3] * c;                                              \
  x4 = decode[4] * c;                                              \
  x5 = decode[5] * c;                                              \
  x6 = decode[6] * c;                                              \
  c = hc[1];                                                       \
  y0 = decode[7] * c;                                              \
  y1 = decode[8] * c;                                              \
  y2 = decode[9] * c;                                              \
  y3 = decode[10] * c;                                             \
  y4 = decode[11] * c;                                             \
  y5 = decode[12] * c;                                             \
  y6 = decode[13] * c;                                             \
  c = hc[2];                                                       \
  x0 += decode[14] * c;                                            \
  x1 += decode[15] * c;                                            \
  x2 += decode[16] * c;                                            \
  x3 += decode[17] * c;                                            \
  x4 += decode[18] * c;                                            \
  x5 += decode[19] * c;                                            \
  x6 += decode[20] * c;                                            \
  c = hc[3];                                                       \
  y0 += decode[21] * c;                                            \
  y1 += decode[22] * c;                                            \
  y2 += decode[23] * c;                                            \
  y3 += decode[24] * c;                                            \
  y4 += decode[25] * c;                                            \
  y5 += decode[26] * c;                                            \
  y6 += decode[27] * c;

#define TGFX_4_COEFF_CONTINUE_FROM_4(ofs) \
  TGFX_SIMD_NO_UNROLL(decode);            \
  c = hc[0 + (ofs)];                      \
  x0 += decode[0 + (ofs)*7] * c;          \
  x1 += decode[1 + (ofs)*7] * c;          \
  x2 += decode[2 + (ofs)*7] * c;          \
  x3 += decode[3 + (ofs)*7] * c;          \
  x4 += decode[4 + (ofs)*7] * c;          \
  x5 += decode[5 + (ofs)*7] * c;          \
  x6 += decode[6 + (ofs)*7] * c;          \
  c = hc[1 + (ofs)];                      \
  y0 += decode[7 + (ofs)*7] * c;          \
  y1 += decode[8 + (ofs)*7] * c;          \
  y2 += decode[9 + (ofs)*7] * c;          \
  y3 += decode[10 + (ofs)*7] * c;         \
  y4 += decode[11 + (ofs)*7] * c;         \
  y5 += decode[12 + (ofs)*7] * c;         \
  y6 += decode[13 + (ofs)*7] * c;         \
  c = hc[2 + (ofs)];                      \
  x0 += decode[14 + (ofs)*7] * c;         \
  x1 += decode[15 + (ofs)*7] * c;         \
  x2 += decode[16 + (ofs)*7] * c;         \
  x3 += decode[17 + (ofs)*7] * c;         \
  x4 += decode[18 + (ofs)*7] * c;         \
  x5 += decode[19 + (ofs)*7] * c;         \
  x6 += decode[20 + (ofs)*7] * c;         \
  c = hc[3 + (ofs)];                      \
  y0 += decode[21 + (ofs)*7] * c;         \
  y1 += decode[22 + (ofs)*7] * c;         \
  y2 += decode[23 + (ofs)*7] * c;         \
  y3 += decode[24 + (ofs)*7] * c;         \
  y4 += decode[25 + (ofs)*7] * c;         \
  y5 += decode[26 + (ofs)*7] * c;         \
  y6 += decode[27 + (ofs)*7] * c;

#define TGFX_1_COEFF_REMNANT(ofs) \
  TGFX_SIMD_NO_UNROLL(decode);    \
  c = hc[0 + (ofs)];              \
  x0 += decode[0 + (ofs)*7] * c;  \
  x1 += decode[1 + (ofs)*7] * c;  \
  x2 += decode[2 + (ofs)*7] * c;  \
  x3 += decode[3 + (ofs)*7] * c;  \
  x4 += decode[4 + (ofs)*7] * c;  \
  x5 += decode[5 + (ofs)*7] * c;  \
  x6 += decode[6 + (ofs)*7] * c;

#define TGFX_2_COEFF_REMNANT(ofs) \
  TGFX_SIMD_NO_UNROLL(decode);    \
  c = hc[0 + (ofs)];              \
  x0 += decode[0 + (ofs)*7] * c;  \
  x1 += decode[1 + (ofs)*7] * c;  \
  x2 += decode[2 + (ofs)*7] * c;  \
  x3 += decode[3 + (ofs)*7] * c;  \
  x4 += decode[4 + (ofs)*7] * c;  \
  x5 += decode[5 + (ofs)*7] * c;  \
  x6 += decode[6 + (ofs)*7] * c;  \
  c = hc[1 + (ofs)];              \
  y0 += decode[7 + (ofs)*7] * c;  \
  y1 += decode[8 + (ofs)*7] * c;  \
  y2 += decode[9 + (ofs)*7] * c;  \
  y3 += decode[10 + (ofs)*7] * c; \
  y4 += decode[11 + (ofs)*7] * c; \
  y5 += decode[12 + (ofs)*7] * c; \
  y6 += decode[13 + (ofs)*7] * c;

#define TGFX_3_COEFF_REMNANT(ofs) \
  TGFX_SIMD_NO_UNROLL(decode);    \
  c = hc[0 + (ofs)];              \
  x0 += decode[0 + (ofs)*7] * c;  \
  x1 += decode[1 + (ofs)*7] * c;  \
  x2 += decode[2 + (ofs)*7] * c;  \
  x3 += decode[3 + (ofs)*7] * c;  \
  x4 += decode[4 + (ofs)*7] * c;  \
  x5 += decode[5 + (ofs)*7] * c;  \
  x6 += decode[6 + (ofs)*7] * c;  \
  c = hc[1 + (ofs)];              \
  y0 += decode[7 + (ofs)*7] * c;  \
  y1 += decode[8 + (ofs)*7] * c;  \
  y2 += decode[9 + (ofs)*7] * c;  \
  y3 += decode[10 + (ofs)*7] * c; \
  y4 += decode[11 + (ofs)*7] * c; \
  y5 += decode[12 + (ofs)*7] * c; \
  y6 += decode[13 + (ofs)*7] * c; \
  c = hc[2 + (ofs)];              \
  x0 += decode[14 + (ofs)*7] * c; \
  x1 += decode[15 + (ofs)*7] * c; \
  x2 += decode[16 + (ofs)*7] * c; \
  x3 += decode[17 + (ofs)*7] * c; \
  x4 += decode[18 + (ofs)*7] * c; \
  x5 += decode[19 + (ofs)*7] * c; \
  x6 += decode[20 + (ofs)*7] * c;

#define TGFX_STORE_OUTPUT()                   \
  output[0] = x0 + y0;                        \
  output[1] = x1 + y1;                        \
  output[2] = x2 + y2;                        \
  output[3] = x3 + y3;                        \
  output[4] = x4 + y4;                        \
  output[5] = x5 + y5;                        \
  output[6] = x6 + y6;                        \
  horizontalCoefficients += coefficientWidth; \
  ++horizontalContributors;                   \
  output += 7;

#define TGFX_HORIZONTAL_CHANNELS 7
#define TGFX_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCE_FILE

// include all of the vertical resamplers (both scatter and gather versions)

#define TGFX_VERTICAL_CHANNELS 1
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 1
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#define TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 2
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 2
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#define TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 3
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 3
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#define TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 4
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 4
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#define TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 5
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 5
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#define TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 6
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 6
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#define TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 7
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 7
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#define TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 8
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#include SOURCE_FILE

#define TGFX_VERTICAL_CHANNELS 8
#define TGFX_IMAGE_RESIZE_DO_VERTICALS
#define TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCE_FILE

static HorizontalGatherChannelsFunc** HorizontalGatherNCoeffsFuncs[8] = {
    0,
    HorizontalGather1ChannelsWithNCoeffsFuncs,
    HorizontalGather2ChannelsWithNCoeffsFuncs,
    HorizontalGather3ChannelsWithNCoeffsFuncs,
    HorizontalGather4ChannelsWithNCoeffsFuncs,
    0,
    0,
    HorizontalGather7ChannelsWithNCoeffsFuncs};

static HorizontalGatherChannelsFunc** HorizontalGatherChannelsFuncs[8] = {
    0,
    HorizontalGather1ChannelsFuncs,
    HorizontalGather2ChannelsFuncs,
    HorizontalGather3ChannelsFuncs,
    HorizontalGather4ChannelsFuncs,
    0,
    0,
    HorizontalGather7ChannelsFuncs};

static int EdgeWrap(int n, int max) {
  // avoid per-pixel switch
  if (n >= 0 && n < max) return n;
  return EdgeClampFull(n, max);
}

// get information on the extents of a sampler
static void GetExtents(Sampler* samp, Extents* scanlineExtents) {
  int j, stop;
  int leftMargin, rightMargin;
  int minN = 0x7fffffff, maxN = -0x7fffffff;
  int minLeft = 0x7fffffff, maxLeft = -0x7fffffff;
  int minRight = 0x7fffffff, maxRight = -0x7fffffff;
  Contributors* contributors = samp->contributors;
  int outputSubSize = samp->scaleInfo.outputSubSize;
  int inputFullSize = samp->scaleInfo.inputFullSize;
  int filterPixelMargin = samp->filterPixelMargin;

  assert(samp->isGather);

  stop = outputSubSize;
  for (j = 0; j < stop; j++) {
    assert(contributors[j].n1 >= contributors[j].n0);
    if (contributors[j].n0 < minN) {
      minN = contributors[j].n0;
      stop = j + filterPixelMargin;  // if we find a new min, only scan another filter width
      if (stop > outputSubSize) stop = outputSubSize;
    }
  }

  stop = 0;
  for (j = outputSubSize - 1; j >= stop; j--) {
    assert(contributors[j].n1 >= contributors[j].n0);
    if (contributors[j].n1 > maxN) {
      maxN = contributors[j].n1;
      stop = j - filterPixelMargin;  // if we find a new max, only scan another filter width
      if (stop < 0) stop = 0;
    }
  }

  assert(scanlineExtents->conservative.n0 <= minN);
  assert(scanlineExtents->conservative.n1 >= maxN);

  // now calculate how much into the margins we really read
  leftMargin = 0;
  if (minN < 0) {
    leftMargin = -minN;
    minN = 0;
  }

  rightMargin = 0;
  if (maxN >= inputFullSize) {
    rightMargin = maxN - inputFullSize + 1;
    maxN = inputFullSize - 1;
  }

  // index 1 is margin pixel extents (how many pixels we hang over the edge)
  scanlineExtents->edgeSizes[0] = leftMargin;
  scanlineExtents->edgeSizes[1] = rightMargin;

  // index 2 is pixels read from the input
  scanlineExtents->spans[0].n0 = minN;
  scanlineExtents->spans[0].n1 = maxN;
  scanlineExtents->spans[0].pixelOffsetForInput = minN;

  // default to no other input range
  scanlineExtents->spans[1].n0 = 0;
  scanlineExtents->spans[1].n1 = -1;
  scanlineExtents->spans[1].pixelOffsetForInput = 0;

  // convert margin pixels to the pixels within the input (min and max)
  for (j = -leftMargin; j < 0; j++) {
    int p = EdgeWrap(j, inputFullSize);
    if (p < minLeft) minLeft = p;
    if (p > maxLeft) maxLeft = p;
  }

  for (j = inputFullSize; j < (inputFullSize + rightMargin); j++) {
    int p = EdgeWrap(j, inputFullSize);
    if (p < minRight) minRight = p;
    if (p > maxRight) maxRight = p;
  }

  // merge the left margin pixel region if it connects within 4 pixels of main pixel region
  if (minLeft != 0x7fffffff) {
    if (((minLeft <= minN) && ((maxLeft + TGFX_MERGE_RUNS_PIXEL_THRESHOLD) >= minN)) ||
        ((minN <= minLeft) && ((maxN + TGFX_MERGE_RUNS_PIXEL_THRESHOLD) >= maxLeft))) {
      scanlineExtents->spans[0].n0 = minN = TGFX_MIN(minN, minLeft);
      scanlineExtents->spans[0].n1 = maxN = TGFX_MAX(maxN, maxLeft);
      scanlineExtents->spans[0].pixelOffsetForInput = minN;
      leftMargin = 0;
    }
  }

  // merge the right margin pixel region if it connects within 4 pixels of main pixel region
  if (minRight != 0x7fffffff) {
    if (((minRight <= minN) && ((maxRight + TGFX_MERGE_RUNS_PIXEL_THRESHOLD) >= minN)) ||
        ((minN <= minRight) && ((maxN + TGFX_MERGE_RUNS_PIXEL_THRESHOLD) >= maxRight))) {
      scanlineExtents->spans[0].n0 = minN = TGFX_MIN(minN, minRight);
      scanlineExtents->spans[0].n1 = maxN = TGFX_MAX(maxN, maxRight);
      scanlineExtents->spans[0].pixelOffsetForInput = minN;
      rightMargin = 0;
    }
  }

  assert(scanlineExtents->conservative.n0 <= minN);
  assert(scanlineExtents->conservative.n1 >= maxN);

  // you get two ranges when you have the WRAP edge mode and you are doing just the a piece of the resize
  //   so you need to get a second run of pixels from the opposite side of the scanline (which you
  //   wouldn't need except for WRAP)

  // if we can't merge the minLeft range, add it as a second range
  if ((leftMargin) && (minLeft != 0x7fffffff)) {
    Span* newspan = scanlineExtents->spans + 1;
    assert(rightMargin == 0);
    if (minLeft < scanlineExtents->spans[0].n0) {
      scanlineExtents->spans[1].pixelOffsetForInput = scanlineExtents->spans[0].n0;
      scanlineExtents->spans[1].n0 = scanlineExtents->spans[0].n0;
      scanlineExtents->spans[1].n1 = scanlineExtents->spans[0].n1;
      --newspan;
    }
    newspan->pixelOffsetForInput = minLeft;
    newspan->n0 = -leftMargin;
    newspan->n1 = (maxLeft - minLeft) - leftMargin;
    scanlineExtents->edgeSizes[0] =
        0;  // don't need to copy the left margin, since we are directly decoding into the margin
  }
  // if we can't merge the minLeft range, add it as a second range
  else if ((rightMargin) && (minRight != 0x7fffffff)) {
    Span* newspan = scanlineExtents->spans + 1;
    if (minRight < scanlineExtents->spans[0].n0) {
      scanlineExtents->spans[1].pixelOffsetForInput = scanlineExtents->spans[0].n0;
      scanlineExtents->spans[1].n0 = scanlineExtents->spans[0].n0;
      scanlineExtents->spans[1].n1 = scanlineExtents->spans[0].n1;
      --newspan;
    }
    newspan->pixelOffsetForInput = minRight;
    newspan->n0 = scanlineExtents->spans[1].n1 + 1;
    newspan->n1 = scanlineExtents->spans[1].n1 + 1 + (maxRight - minRight);
    // don't need to copy the right margin, since we are directly decoding into the margin
    scanlineExtents->edgeSizes[1] = 0;
  }

  // sort the spans into write output order
  if ((scanlineExtents->spans[1].n1 > scanlineExtents->spans[1].n0) &&
      (scanlineExtents->spans[0].n0 > scanlineExtents->spans[1].n0)) {
    Span tspan = scanlineExtents->spans[0];
    scanlineExtents->spans[0] = scanlineExtents->spans[1];
    scanlineExtents->spans[1] = tspan;
  }
}

static int PackCoefficients(int numContibutors, Contributors* contributors, float* coefficents,
                            int coefficientWidth, int widest, int row0, int row1) {
#define TGFX_MOVE_1(dest, src)                      \
  {                                                 \
    TGFX_NO_UNROLL(dest);                           \
    ((uint32_t*)(dest))[0] = ((uint32_t*)(src))[0]; \
  }
#define TGFX_MOVE_2(dest, src)                      \
  {                                                 \
    TGFX_NO_UNROLL(dest);                           \
    ((uint64_t*)(dest))[0] = ((uint64_t*)(src))[0]; \
  }
#define TGFX_MOVE_4(dest, src)                      \
  {                                                 \
    TGFX_NO_UNROLL(dest);                           \
    ((uint64_t*)(dest))[0] = ((uint64_t*)(src))[0]; \
    ((uint64_t*)(dest))[1] = ((uint64_t*)(src))[1]; \
  }
  int rowEnd = row1 + 1;  // only used in an assert

  if (coefficientWidth != widest) {
    float* pc = coefficents;
    float* coeffs = coefficents;
    float* pc_end = coefficents + numContibutors * widest;
    switch (widest) {
      case 1:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_1(pc, coeffs);
          ++pc;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 2:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_2(pc, coeffs);
          pc += 2;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 3:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_2(pc, coeffs);
          TGFX_MOVE_1(pc + 2, coeffs + 2);
          pc += 3;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 4:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_4(pc, coeffs);
          pc += 4;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 5:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_4(pc, coeffs);
          TGFX_MOVE_1(pc + 4, coeffs + 4);
          pc += 5;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 6:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_4(pc, coeffs);
          TGFX_MOVE_2(pc + 4, coeffs + 4);
          pc += 6;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 7:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_4(pc, coeffs);
          TGFX_MOVE_2(pc + 4, coeffs + 4);
          TGFX_MOVE_1(pc + 6, coeffs + 6);
          pc += 7;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 8:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_4(pc, coeffs);
          TGFX_MOVE_4(pc + 4, coeffs + 4);
          pc += 8;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 9:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_4(pc, coeffs);
          TGFX_MOVE_4(pc + 4, coeffs + 4);
          TGFX_MOVE_1(pc + 8, coeffs + 8);
          pc += 9;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 10:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_4(pc, coeffs);
          TGFX_MOVE_4(pc + 4, coeffs + 4);
          TGFX_MOVE_2(pc + 8, coeffs + 8);
          pc += 10;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 11:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_4(pc, coeffs);
          TGFX_MOVE_4(pc + 4, coeffs + 4);
          TGFX_MOVE_2(pc + 8, coeffs + 8);
          TGFX_MOVE_1(pc + 10, coeffs + 10);
          pc += 11;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      case 12:
        TGFX_NO_UNROLL_LOOP_START
        do {
          TGFX_MOVE_4(pc, coeffs);
          TGFX_MOVE_4(pc + 4, coeffs + 4);
          TGFX_MOVE_4(pc + 8, coeffs + 8);
          pc += 12;
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
      default:
        TGFX_NO_UNROLL_LOOP_START
        do {
          float* copy_end = pc + widest - 4;
          float* c = coeffs;
          do {
            TGFX_NO_UNROLL(pc);
            TGFX_MOVE_4(pc, c);
            pc += 4;
            c += 4;
          } while (pc <= copy_end);
          copy_end += 4;
          TGFX_NO_UNROLL_LOOP_START
          while (pc < copy_end) {
            TGFX_MOVE_1(pc, c);
            ++pc;
            ++c;
          }
          coeffs += coefficientWidth;
        } while (pc < pc_end);
        break;
    }
  }

  // some horizontal routines read one float off the end (which is then masked off), so put in a
  // sentinal so we don't read an snan or denormal
  coefficents[widest * numContibutors] = 8888.0f;

  // the minimum we might read for unrolled filters widths is 12. So, we need to make sure we never
  // read outside the decode buffer, by possibly moving the sample area back into the scanline, and
  // putting zeros weights first. we start on the right edge and check until we're well past the
  // possible clip area (2*widest).
  {
    Contributors* contribs = contributors + numContibutors - 1;
    float* coeffs = coefficents + widest * (numContibutors - 1);

    // go until no chance of clipping (this is usually less than 8 lops)
    while ((contribs >= contributors) && ((contribs->n0 + widest * 2) >= rowEnd)) {
      // might we clip??
      if ((contribs->n0 + widest) > rowEnd) {
        int stopRange = widest;

        // if range is larger than 12, it will be handled by generic loops that can terminate on the
        // exact length of this contrib n1, instead of a fixed widest amount - so calculate this
        if (widest > 12) {
          int mod;

          // how far will be read in the n_coeff loop (which depends on the widest count mod4);
          mod = widest & 3;
          stopRange = (((contribs->n1 - contribs->n0 + 1) - mod + 3) & ~3) + mod;

          // the n_coeff loops do a minimum amount of coeffs, so factor that in!
          if (stopRange < (8 + mod)) stopRange = 8 + mod;
        }

        // now see if we still clip with the refined range
        if ((contribs->n0 + stopRange) > rowEnd) {
          int newN0 = rowEnd - stopRange;
          int num = contribs->n1 - contribs->n0 + 1;
          int backup = contribs->n0 - newN0;
          float* fromCo = coeffs + num - 1;
          float* toCo = fromCo + backup;

          assert((newN0 >= row0) && (newN0 < contribs->n0));

          // move the coeffs over
          while (num) {
            *toCo-- = *fromCo--;
            --num;
          }
          // zero new positions
          while (toCo >= coeffs) *toCo-- = 0;
          // set new start point
          contribs->n0 = newN0;
          if (widest > 12) {
            int mod;

            // how far will be read in the n_coeff loop (which depends on the widest count mod4);
            mod = widest & 3;
            stopRange = (((contribs->n1 - contribs->n0 + 1) - mod + 3) & ~3) + mod;

            // the n_coeff loops do a minimum amount of coeffs, so factor that in!
            if (stopRange < (8 + mod)) stopRange = 8 + mod;
          }
        }
      }
      --contribs;
      coeffs -= widest;
    }
  }

  return widest;
#undef TGFX_MOVE_1
#undef TGFX_MOVE_2
#undef TGFX_MOVE_4
}

static void GetSplitInfo(PerSplitInfo* splitInfo, int splits, int outputHeight,
                         int verticalPixelMargin, int inputFullHeight) {
  int i, cur;
  int left = outputHeight;

  cur = 0;
  for (i = 0; i < splits; i++) {
    int each;
    splitInfo[i].startOutputY = cur;
    each = left / (splits - i);
    splitInfo[i].endOutputY = cur + each;
    cur += each;
    left -= each;

    // scatter range (updated to minimum as you run it)
    splitInfo[i].startInputY = -verticalPixelMargin;
    splitInfo[i].endInputY = inputFullHeight + verticalPixelMargin;
  }
}

static ResizeInfo* AllocInternalMemAndBuildSamplers(Sampler* horizontal, Sampler* vertical,
                                                    Contributors* conservative,
                                                    PixelLayout inputPixelLayoutPublic,
                                                    PixelLayout outputPixelLayoutPublic, int splits,
                                                    int newX, int newY) {
  static char ChannelCountIndex[8] = {9, 0, 1, 2, 3, 9, 9, 4};

  ResizeInfo* info = 0;
  void* alloced = 0;
  size_t allocedTotal = 0;
  int verticalFirst;
  size_t decodeBufferSize, ringBufferLengthBytes, ringBufferSize, verticalBufferSize;
  int allocRingBufferNumEntries;

  int alphaWeightingType = 0;  // 0=none, 1=simple, 2=fancy
  int conservativeSplitOutputSize = GetMaxSplit(splits, vertical->scaleInfo.outputSubSize);
  InternalPixelLayout inputPixelLayout =
      PixelLayoutConvertPublicToInternal[static_cast<size_t>(inputPixelLayoutPublic)];
  InternalPixelLayout outputPixelLayout =
      PixelLayoutConvertPublicToInternal[static_cast<size_t>(outputPixelLayoutPublic)];
  int channels = PixelChannels[static_cast<size_t>(inputPixelLayout)];
  int effectiveChannels = channels;

  if ((inputPixelLayout >= InternalPixelLayout::RGBA) &&
      (inputPixelLayout <= InternalPixelLayout::AR) &&
      (outputPixelLayout >= InternalPixelLayout::RGBA) &&
      (outputPixelLayout <= InternalPixelLayout::AR)) {
    static int fancy_alpha_effective_cnts[6] = {7, 7, 7, 7, 3, 3};
    alphaWeightingType = 2;
    effectiveChannels = fancy_alpha_effective_cnts[static_cast<size_t>(inputPixelLayout) -
                                                   static_cast<size_t>(InternalPixelLayout::RGBA)];
  } else if ((inputPixelLayout >= InternalPixelLayout::RGBA_PM) &&
             (inputPixelLayout <= InternalPixelLayout::AR_PM) &&
             (outputPixelLayout >= InternalPixelLayout::RGBA) &&
             (outputPixelLayout <= InternalPixelLayout::AR)) {
    // input premult, output non-premult
    alphaWeightingType = 3;
  } else if ((inputPixelLayout >= InternalPixelLayout::RGBA) &&
             (inputPixelLayout <= InternalPixelLayout::AR) &&
             (outputPixelLayout >= InternalPixelLayout::RGBA_PM) &&
             (outputPixelLayout <= InternalPixelLayout::AR_PM)) {
    // input non-premult, output premult
    alphaWeightingType = 1;
  }

  // channel in and out count must match currently
  if (channels != PixelChannels[static_cast<size_t>(outputPixelLayout)]) return 0;

  // get vertical first
  verticalFirst = ShouldDoVerticalFirst(
      ComputeWeights[(int)ChannelCountIndex[effectiveChannels]], horizontal->filterPixelWidth,
      horizontal->scaleInfo.scale, horizontal->scaleInfo.outputSubSize, vertical->filterPixelWidth,
      vertical->scaleInfo.scale, vertical->scaleInfo.outputSubSize, vertical->isGather);

  // sometimes read one float off in some of the unrolled loops (with a weight of zero coeff, so it doesn't have an effect)
  //   we use a few extra floats instead of just 1, so that input callback buffer can overlap with the decode buffer without
  //   the conversion routines overwriting the callback input data.

  // extra floats for input callback stagger
  decodeBufferSize =
      (unsigned long)((conservative->n1 - conservative->n0 + 1) * effectiveChannels) *
          sizeof(float) +
      sizeof(float) * TGFX_INPUT_CALLBACK_PADDING;

  // extra floats for padding
  ringBufferLengthBytes =
      (size_t)horizontal->scaleInfo.outputSubSize * (size_t)effectiveChannels * sizeof(float) +
      sizeof(float) * TGFX_INPUT_CALLBACK_PADDING;

  // if we do vertical first, the ring buffer holds a whole decoded line
  if (verticalFirst) {
    ringBufferLengthBytes = (decodeBufferSize + 15) & (size_t)~15;
  }

  if ((ringBufferLengthBytes & 4095) == 0) ringBufferLengthBytes += 64 * 3;  // avoid 4k alias

  // One extra entry because floating point precision problems sometimes cause an extra to be necessary.
  allocRingBufferNumEntries = vertical->filterPixelWidth + 1;

  // we never need more ring buffer entries than the scanlines we're outputting when in scatter mode
  if ((!vertical->isGather) && (allocRingBufferNumEntries > conservativeSplitOutputSize))
    allocRingBufferNumEntries = conservativeSplitOutputSize;

  ringBufferSize = (size_t)allocRingBufferNumEntries * (size_t)ringBufferLengthBytes;

  // The vertical buffer is used differently, depending on whether we are scattering
  //   the vertical scanlines, or gathering them.
  //   If scattering, it's used at the temp buffer to accumulate each output.
  //   If gathering, it's just the output buffer.
  verticalBufferSize =
      (size_t)horizontal->scaleInfo.outputSubSize * (size_t)effectiveChannels * sizeof(float) +
      sizeof(float);  // extra float for padding

  // we make two passes through this loop, 1st to add everything up, 2nd to allocate and init
  for (;;) {
    int i;
    void* advanceMem = alloced;
    int copyHorizontal = 0;
    Sampler* possiblyUseHorizontalForPivot = 0;

#define TGFX_NEXT_PTR(ptr, size, ntype)                            \
  advanceMem = (void*)((((size_t)advanceMem) + 15) & (size_t)~15); \
  if (alloced) ptr = (ntype*)advanceMem;                           \
  advanceMem = ((char*)advanceMem) + (size);

    TGFX_NEXT_PTR(info, sizeof(ResizeInfo), ResizeInfo);

    TGFX_NEXT_PTR(info->splitInfo, sizeof(PerSplitInfo) * (unsigned long)splits, PerSplitInfo);

    if (info) {
      static AlphaWeightFunc* fancyAlphaWeights[6] = {FancyAlphaWeight4ch, FancyAlphaWeight4ch,
                                                      FancyAlphaWeight4ch, FancyAlphaWeight4ch,
                                                      FancyAlphaWeight2ch, FancyAlphaWeight2ch};
      static AlphaUnweightFunc* fancyAlphaUnweights[6] = {
          FancyAlphaUnweight4ch, FancyAlphaUnweight4ch, FancyAlphaUnweight4ch,
          FancyAlphaUnweight4ch, FancyAlphaUnweight2ch, FancyAlphaUnweight2ch};
      static AlphaWeightFunc* simpleAlphaWeights[6] = {SimpleAlphaWeight4ch, SimpleAlphaWeight4ch,
                                                       SimpleAlphaWeight4ch, SimpleAlphaWeight4ch,
                                                       SimpleAlphaWeight2ch, SimpleAlphaWeight2ch};
      static AlphaUnweightFunc* simpleAlphaUnweights[6] = {
          SimpleAlphaUnweight4ch, SimpleAlphaUnweight4ch, SimpleAlphaUnweight4ch,
          SimpleAlphaUnweight4ch, SimpleAlphaUnweight2ch, SimpleAlphaUnweight2ch};

      // initialize info fields
      info->allocedMem = alloced;
      info->allocedTotal = allocedTotal;

      info->channels = channels;
      info->effectiveChannels = effectiveChannels;

      info->offsetX = newX;
      info->offsetY = newY;
      info->allocRingBufferNumEntries = (int)allocRingBufferNumEntries;
      info->ringBufferNumEntries = 0;
      info->ringBufferLengthBytes = (int)ringBufferLengthBytes;
      info->splits = splits;
      info->verticalFirst = verticalFirst;

      info->inputPixelLayoutInternal = inputPixelLayout;
      info->outputPixelLayoutInternal = outputPixelLayout;

      // setup alpha weight functions
      info->alphaWeight = 0;
      info->alphaUnweight = 0;

      // handle alpha weighting functions and overrides
      if (alphaWeightingType == 2) {
        // high quality alpha multiplying on the way in, dividing on the way out
        info->alphaWeight = fancyAlphaWeights[static_cast<size_t>(inputPixelLayout) -
                                              static_cast<size_t>(InternalPixelLayout::RGBA)];
        info->alphaUnweight = fancyAlphaUnweights[static_cast<size_t>(outputPixelLayout) -
                                                  static_cast<size_t>(InternalPixelLayout::RGBA)];
      } else if (alphaWeightingType == 1) {
        // fast alpha on the way in, leave in premultiplied form on way out
        info->alphaWeight = simpleAlphaWeights[static_cast<size_t>(inputPixelLayout) -
                                               static_cast<size_t>(InternalPixelLayout::RGBA)];
      } else if (alphaWeightingType == 3) {
        // incoming is premultiplied, fast alpha dividing on the way out - non-premultiplied output
        info->alphaUnweight = simpleAlphaUnweights[static_cast<size_t>(outputPixelLayout) -
                                                   static_cast<size_t>(InternalPixelLayout::RGBA)];
      }

      // handle 3-chan color flipping, using the alpha weight path
      if (((inputPixelLayout == InternalPixelLayout::RGB) &&
           (outputPixelLayout == InternalPixelLayout::BGR)) ||
          ((inputPixelLayout == InternalPixelLayout::BGR) &&
           (outputPixelLayout == InternalPixelLayout::RGB))) {
        // do the flipping on the smaller of the two ends
        if (horizontal->scaleInfo.scale < 1.0f) info->alphaUnweight = SimpleFlip3ch;
        else
          info->alphaWeight = SimpleFlip3ch;
      }
    }

    // get all the per-split buffers
    for (i = 0; i < splits; i++) {
      TGFX_NEXT_PTR(info->splitInfo[i].decodeBuffer, decodeBufferSize, float);
      TGFX_NEXT_PTR(info->splitInfo[i].ringBuffer, ringBufferSize, float);
      TGFX_NEXT_PTR(info->splitInfo[i].verticalBuffer, verticalBufferSize, float);
    }

    // alloc memory for to-be-pivoted coeffs (if necessary)
    if (vertical->isGather == 0) {
      size_t both;
      size_t tempMemAmt;

      // when in vertical scatter mode, we first build the coefficients in gather mode, and then pivot after,
      //   that means we need two buffers, so we try to use the decode buffer and ring buffer for this. if that
      //   is too small, we just allocate extra memory to use as this temp.

      both = (size_t)vertical->gatherPrescatterContibutorsSize +
             (size_t)vertical->gatherPrescatterCoefficientsSize;
      tempMemAmt =
          (size_t)(decodeBufferSize + ringBufferSize + verticalBufferSize) * (size_t)splits;
      if (tempMemAmt >= both) {
        if (info) {
          vertical->gatherPrescatterContibutors = (Contributors*)info->splitInfo[0].decodeBuffer;
          vertical->gatherPrescatterCoefficients =
              (float*)(((char*)info->splitInfo[0].decodeBuffer) +
                       vertical->gatherPrescatterContibutorsSize);
        }
      } else {
        // ring+decode memory is too small, so allocate temp memory
        TGFX_NEXT_PTR(vertical->gatherPrescatterContibutors,
                      vertical->gatherPrescatterContibutorsSize, Contributors);
        TGFX_NEXT_PTR(vertical->gatherPrescatterCoefficients,
                      vertical->gatherPrescatterCoefficientsSize, float);
      }
    }

    TGFX_NEXT_PTR(horizontal->contributors, horizontal->contributorsSize, Contributors);
    TGFX_NEXT_PTR(horizontal->coefficients, horizontal->coefficientsSize, float);

    // are the two filters identical?? (happens a lot with mipmap generation)
    if (horizontal->scaleInfo.outputSubSize == vertical->scaleInfo.outputSubSize) {
      float diffScale = horizontal->scaleInfo.scale - vertical->scaleInfo.scale;
      float diffShift = horizontal->scaleInfo.pixelShift - vertical->scaleInfo.pixelShift;
      if (diffScale < 0.0f) diffScale = -diffScale;
      if (diffShift < 0.0f) diffShift = -diffShift;
      if ((diffScale <= SMALL_FLOAT) && (diffShift <= SMALL_FLOAT)) {
        if (horizontal->isGather == vertical->isGather) {
          copyHorizontal = 1;
          goto NoVertAlloc;
        }
        // everything matches, but vertical is scatter, horizontal is gather, use horizontal coeffs
        // for vertical pivot coeffs
        possiblyUseHorizontalForPivot = horizontal;
      }
    }

    TGFX_NEXT_PTR(vertical->contributors, vertical->contributorsSize, Contributors);
    TGFX_NEXT_PTR(vertical->coefficients, vertical->coefficientsSize, float);

  NoVertAlloc:

    if (info) {

      CalculateFilters(horizontal, 0);

      // setup the horizontal gather functions
      // start with defaulting to the n_coeffs functions (specialized on channels and remnant leftover)
      info->horizontalGatherchannels =
          HorizontalGatherNCoeffsFuncs[effectiveChannels][horizontal->extentInfo.widest & 3];
      // but if the number of coeffs <= 12, use another set of special cases. <=12 coeffs is any enlarging resize, or shrinking resize down to about 1/3 size
      if (horizontal->extentInfo.widest <= 12)
        info->horizontalGatherchannels =
            HorizontalGatherChannelsFuncs[effectiveChannels][horizontal->extentInfo.widest - 1];

      info->scanlineExtents.conservative.n0 = conservative->n0;
      info->scanlineExtents.conservative.n1 = conservative->n1;

      // get exact extents
      GetExtents(horizontal, &info->scanlineExtents);

      // pack the horizontal coeffs
      horizontal->coefficientWidth = PackCoefficients(
          horizontal->numContibutors, horizontal->contributors, horizontal->coefficients,
          horizontal->coefficientWidth, horizontal->extentInfo.widest,
          info->scanlineExtents.conservative.n0, info->scanlineExtents.conservative.n1);

      memcpy(&info->horizontal, horizontal, sizeof(Sampler));

      if (copyHorizontal) {
        memcpy(&info->vertical, horizontal, sizeof(Sampler));
      } else {

        CalculateFilters(vertical, possiblyUseHorizontalForPivot);
        memcpy(&info->vertical, vertical, sizeof(Sampler));
      }

      // setup the vertical split ranges
      GetSplitInfo(info->splitInfo, info->splits, info->vertical.scaleInfo.outputSubSize,
                   info->vertical.filterPixelMargin, info->vertical.scaleInfo.inputFullSize);

      // now we know precisely how many entries we need
      info->ringBufferNumEntries = info->vertical.extentInfo.widest;

      // we never need more ring buffer entries than the scanlines we're outputting
      if ((!info->vertical.isGather) && (info->ringBufferNumEntries > conservativeSplitOutputSize))
        info->ringBufferNumEntries = conservativeSplitOutputSize;
      assert(info->ringBufferNumEntries <= info->allocRingBufferNumEntries);
    }
#undef TGFX_NEXT_PTR

    // is this the first time through loop?
    if (info == 0) {
      allocedTotal = (15 + (size_t)advanceMem);
      alloced = malloc(allocedTotal);
      if (alloced == 0) return 0;
    } else
      return info;  // success
  }
}

static void UpdateInfoFromResize(ResizeInfo* info, ResizeData* resize) {
  static DecodePixelsFunc*
      DecodeSimple[(size_t)DataType::HALF_FLOAT - (size_t)DataType::UINT8_SRGB + 1] = {
          /* 1ch-4ch */ DecodeUint8SRGB, DecodeUint8SRGB, 0, DecodeFloatLinear,
          DecodeHalfFloatLinear,
      };

  static DecodePixelsFunc*
      DecodeAlphas[static_cast<size_t>(InternalPixelLayout::AR) -
                   static_cast<size_t>(InternalPixelLayout::RGBA) + 1]
                  [(size_t)DataType::HALF_FLOAT - (size_t)DataType::UINT8_SRGB + 1] = {
                      {/* RGBA */ DecodeUint8SRGB4LinearAlpha, DecodeUint8SRGB, 0,
                       DecodeFloatLinear, DecodeHalfFloatLinear},
                      {/* BGRA */ DecodeUint8SRGB4LinearAlphaBGRA, DecodeUint8SRGBBGRA, 0,
                       DecodeFloatLinearBGRA, DecodeHalfFloatLinearBGRA},
                      {/* ARGB */ DecodeUint8SRGB4LinearAlphaARGB, DecodeUint8SRGBARGB, 0,
                       DecodeFloatLinearARGB, DecodeHalfFloatLinearARGB},
                      {/* ABGR */ DecodeUint8SRGB4LinearAlphaABGR, DecodeUint8SRGBABGR, 0,
                       DecodeFloatLinearABGR, DecodeHalfFloatLinearABGR},
                      {/* RA   */ DecodeUint8SRGB2LinearAlpha, DecodeUint8SRGB, 0,
                       DecodeFloatLinear, DecodeHalfFloatLinear},
                      {/* AR   */ DecodeUint8SRGB2LinearAlphaAR, DecodeUint8SRGBAR, 0,
                       DecodeFloatLinearAR, DecodeHalfFloatLinearAR},
                  };

  static DecodePixelsFunc* DecodeSimpleScaledOrNot[2][2] = {
      {DecodeUint8LinearScaled, DecodeUint8Linear},
      {DecodeUint16LinearScaled, DecodeUint16Linear},
  };

  static DecodePixelsFunc* DecodeAlphaScaledOrNot[static_cast<size_t>(InternalPixelLayout::AR) -
                                                  static_cast<size_t>(InternalPixelLayout::RGBA) +
                                                  1][2][2] = {
      {/* RGBA */ {DecodeUint8LinearScaled, DecodeUint8Linear},
       {DecodeUint16LinearScaled, DecodeUint16Linear}},
      {/* BGRA */ {DecodeUint8LinearScaledBGRA, DecodeUint8LinearBGRA},
       {DecodeUint16LinearScaledBGRA, DecodeUint16LinearBGRA}},
      {/* ARGB */ {DecodeUint8LinearScaledARGB, DecodeUint8LinearARGB},
       {DecodeUint16LinearScaledARGB, DecodeUint16LinearARGB}},
      {/* ABGR */ {DecodeUint8LinearScaledABGR, DecodeUint8LinearABGR},
       {DecodeUint16LinearScaledABGR, DecodeUint16LinearABGR}},
      {/* RA   */ {DecodeUint8LinearScaled, DecodeUint8Linear},
       {DecodeUint16LinearScaled, DecodeUint16Linear}},
      {/* AR   */ {DecodeUint8LinearScaledAR, DecodeUint8LinearAR},
       {DecodeUint16LinearScaledAR, DecodeUint16LinearAR}}};

  static EncodePixelsFunc*
      EncodeSimple[(size_t)DataType::HALF_FLOAT - (size_t)DataType::UINT8_SRGB + 1] = {
          /* 1ch-4ch */ EncodeUint8SRGB, EncodeUint8SRGB, 0, EncodeFloatLinear,
          EncodeHalfFloatLinear,
      };

  static EncodePixelsFunc*
      EncodeAlphas[static_cast<size_t>(InternalPixelLayout::AR) -
                   static_cast<size_t>(InternalPixelLayout::RGBA) + 1]
                  [(size_t)DataType::HALF_FLOAT - (size_t)DataType::UINT8_SRGB + 1] = {
                      {/* RGBA */ EncodeUint8SRGB4LinearAlpha, EncodeUint8SRGB, 0,
                       EncodeFloatLinear, EncodeHalfFloatLinear},
                      {/* BGRA */ EncodeUint8SRGB4LinearAlphaBGRA, EncodeUint8SRGBBGRA, 0,
                       EncodeFloatLinearBGRA, EncodeHalfFloatLinearBGRA},
                      {/* ARGB */ EncodeUint8SRGB4LinearAlphaARGB, EncodeUint8SRGBARGB, 0,
                       EncodeFloatLinearARGB, EncodeHalfFloatLinearARGB},
                      {/* ABGR */ EncodeUint8SRGB4LinearAlphaABGR, EncodeUint8SRGBABGR, 0,
                       EncodeFloatLinearABGR, EncodeHalfFloatLinearABGR},
                      {/* RA   */ EncodeUint8SRGB2LinearAlpha, EncodeUint8SRGB, 0,
                       EncodeFloatLinear, EncodeHalfFloatLinear},
                      {/* AR   */ EncodeUint8SRGB2LinearAlphaAR, EncodeUint8SRGBAR, 0,
                       EncodeFloatLinearAR, EncodeHalfFloatLinearAR}};

  static EncodePixelsFunc* EncodeSimpleScaledOrNot[2][2] = {
      {EncodeUint8LinearScaled, EncodeUint8Linear},
      {EncodeUint16LinearScaled, EncodeUint16Linear},
  };

  static EncodePixelsFunc* EncodeAlphasScaledOrNot[static_cast<size_t>(InternalPixelLayout::AR) -
                                                   static_cast<size_t>(InternalPixelLayout::RGBA) +
                                                   1][2][2] = {
      {/* RGBA */ {EncodeUint8LinearScaled, EncodeUint8Linear},
       {EncodeUint16LinearScaled, EncodeUint16Linear}},
      {/* BGRA */ {EncodeUint8LinearScaledBGRA, EncodeUint8LinearBGRA},
       {EncodeUint16LinearScaledBGRA, EncodeUint16LinearBGRA}},
      {/* ARGB */ {EncodeUint8LinearScaledARGB, EncodeUint8LinearARGB},
       {EncodeUint16LinearScaledARGB, EncodeUint16LinearARGB}},
      {/* ABGR */ {EncodeUint8LinearScaledABGR, EncodeUint8LinearABGR},
       {EncodeUint16LinearScaledABGR, EncodeUint16LinearABGR}},
      {/* RA   */ {EncodeUint8LinearScaled, EncodeUint8Linear},
       {EncodeUint16LinearScaled, EncodeUint16Linear}},
      {/* AR   */ {EncodeUint8LinearScaledAR, EncodeUint8LinearAR},
       {EncodeUint16LinearScaledAR, EncodeUint16LinearAR}}};

  DecodePixelsFunc* decodePixels = 0;
  EncodePixelsFunc* encodePixels = 0;
  DataType inputType, outputType;

  inputType = resize->inputDataType;
  outputType = resize->outputDataType;
  info->inputData = resize->inputPixels;
  info->inputStrideBytes = resize->inputStrideInBytes;
  info->outputStrideBytes = resize->outputStrideInBytes;

  // recalc the output and input strides
  if (info->inputStrideBytes == 0)
    info->inputStrideBytes =
        info->channels * info->horizontal.scaleInfo.inputFullSize * TypeSize[(size_t)inputType];

  if (info->outputStrideBytes == 0)
    info->outputStrideBytes =
        info->channels * info->horizontal.scaleInfo.outputSubSize * TypeSize[(size_t)outputType];

  // calc offset
  info->outputData = ((char*)resize->outputPixels) +
                     ((size_t)info->offsetY * (size_t)resize->outputStrideInBytes) +
                     (info->offsetX * info->channels * TypeSize[(size_t)outputType]);

  // setup the input format converters
  if ((inputType == DataType::UINT8) || (inputType == DataType::UINT16)) {
    int nonScaled = 0;

    // check if we can run unscaled - 0-255.0/0-65535.0 instead of 0-1.0 (which is a tiny bit faster when doing linear 8->8 or 16->16)
    if ((!info->alphaWeight) &&
        (!info->alphaUnweight))  // don't short circuit when alpha weighting (get everything to 0-1.0 as usual)
      if (((inputType == DataType::UINT8) && (outputType == DataType::UINT8)) ||
          ((inputType == DataType::UINT16) && (outputType == DataType::UINT16)))
        nonScaled = 1;

    if (info->inputPixelLayoutInternal <= InternalPixelLayout::CHANNEL_4)
      decodePixels = DecodeSimpleScaledOrNot[inputType == DataType::UINT16][nonScaled];
    else
      decodePixels = DecodeAlphaScaledOrNot[(static_cast<size_t>(info->inputPixelLayoutInternal) -
                                             static_cast<size_t>(InternalPixelLayout::RGBA)) %
                                            (static_cast<size_t>(InternalPixelLayout::AR) -
                                             static_cast<size_t>(InternalPixelLayout::RGBA) + 1)]
                                           [inputType == DataType::UINT16][nonScaled];
  } else {
    if (info->inputPixelLayoutInternal <= InternalPixelLayout::CHANNEL_4)
      decodePixels = DecodeSimple[(size_t)inputType - (size_t)DataType::UINT8_SRGB];
    else
      decodePixels = DecodeAlphas[(static_cast<size_t>(info->inputPixelLayoutInternal) -
                                   static_cast<size_t>(InternalPixelLayout::RGBA)) %
                                  (static_cast<size_t>(InternalPixelLayout::AR) -
                                   static_cast<size_t>(InternalPixelLayout::RGBA) + 1)]
                                 [(size_t)inputType - (size_t)DataType::UINT8_SRGB];
  }

  // setup the output format converters
  if ((outputType == DataType::UINT8) || (outputType == DataType::UINT16)) {
    int nonScaled = 0;

    // check if we can run unscaled - 0-255.0/0-65535.0 instead of 0-1.0 (which is a tiny bit faster when doing linear 8->8 or 16->16)
    if ((!info->alphaWeight) &&
        (!info->alphaUnweight))  // don't short circuit when alpha weighting (get everything to 0-1.0 as usual)
      if (((inputType == DataType::UINT8) && (outputType == DataType::UINT8)) ||
          ((inputType == DataType::UINT16) && (outputType == DataType::UINT16)))
        nonScaled = 1;

    if (info->outputPixelLayoutInternal <= InternalPixelLayout::CHANNEL_4)
      encodePixels = EncodeSimpleScaledOrNot[outputType == DataType::UINT16][nonScaled];
    else
      encodePixels = EncodeAlphasScaledOrNot[(static_cast<size_t>(info->outputPixelLayoutInternal) -
                                              static_cast<size_t>(InternalPixelLayout::RGBA)) %
                                             (static_cast<size_t>(InternalPixelLayout::AR) -
                                              static_cast<size_t>(InternalPixelLayout::RGBA) + 1)]
                                            [outputType == DataType::UINT16][nonScaled];
  } else {
    if (info->outputPixelLayoutInternal <= InternalPixelLayout::CHANNEL_4)
      encodePixels = EncodeSimple[(size_t)outputType - (size_t)DataType::UINT8_SRGB];
    else
      encodePixels = EncodeAlphas[(static_cast<size_t>(info->outputPixelLayoutInternal) -
                                   static_cast<size_t>(InternalPixelLayout::RGBA)) %
                                  (static_cast<size_t>(InternalPixelLayout::AR) -
                                   static_cast<size_t>(InternalPixelLayout::RGBA) + 1)]
                                 [(size_t)outputType - (size_t)DataType::UINT8_SRGB];
  }

  info->inputType = inputType;
  info->outputType = outputType;
  info->decodePixels = decodePixels;
  info->encodePixels = encodePixels;
}

#define TGFX_FORCE_MINIMUM_SCANLINES_FOR_SPLITS 4
static int PerformBuild(ResizeData* resize, int splits) {
  Contributors conservative = {0, 0};
  Sampler horizontal, vertical;
  int newOutputSubX, newOutputSubY;
  ResizeInfo* outInfo;

  // have we already built the samplers?
  if (resize->samplers) return 0;

  newOutputSubX = resize->outputSubX;
  newOutputSubY = resize->outputSubY;

  // do horizontal clip and scale calcs
  if (!CalculateRegionTransform(&horizontal.scaleInfo, resize->outputW, &newOutputSubX,
                                resize->outputSubW, resize->inputW, resize->inputS0,
                                resize->inputS1))
    return 0;

  // do vertical clip and scale calcs
  if (!CalculateRegionTransform(&vertical.scaleInfo, resize->outputH, &newOutputSubY,
                                resize->outputSubH, resize->intputH, resize->inputT0,
                                resize->inputT1))
    return 0;

  // if nothing to do, just return
  if ((horizontal.scaleInfo.outputSubSize == 0) || (vertical.scaleInfo.outputSubSize == 0))
    return 0;

  SetSampler(&horizontal, &horizontal.scaleInfo, 1);
  GetConservativeExtents(&horizontal, &conservative);
  SetSampler(&vertical, &vertical.scaleInfo, 0);

  if ((vertical.scaleInfo.outputSubSize / splits) <
      TGFX_FORCE_MINIMUM_SCANLINES_FOR_SPLITS)  // each split should be a minimum of 4 scanlines (handwavey choice)
  {
    splits = vertical.scaleInfo.outputSubSize / TGFX_FORCE_MINIMUM_SCANLINES_FOR_SPLITS;
    if (splits == 0) {
      splits = 1;
    }
  }

  outInfo = AllocInternalMemAndBuildSamplers(
      &horizontal, &vertical, &conservative, resize->inputPixelLayoutPublic,
      resize->outputPixelLaytoutPublic, splits, newOutputSubX, newOutputSubY);

  if (outInfo) {
    resize->splits = splits;
    resize->samplers = outInfo;
    resize->needsRebuild = 0;

    // update anything that can be changed without recalcing samplers
    UpdateInfoFromResize(outInfo, resize);

    return splits;
  }

  return 0;
}

static int BuildSamplersWithSplits(ResizeData* resize, int splits) {
  resize->calledAlloc = 1;
  return PerformBuild(resize, splits);
}

static int BuildSamplers(ResizeData* resize) {
  return BuildSamplersWithSplits(resize, 1);
}

// Get the ring buffer pointer for an index
static float* GetRingBufferEntry(ResizeInfo const* info, PerSplitInfo const* splitInfo, int index) {
  assert(index < info->ringBufferNumEntries);
  return (float*)(((char*)splitInfo->ringBuffer) + (index * info->ringBufferLengthBytes));
}

// Get the specified scan line from the ring buffer
static float* GetRingbufferScanline(ResizeInfo const* info, PerSplitInfo const* splitInfo,
                                    int getScanline) {
  int ringBufferIndex =
      (splitInfo->ringBufferBeginIndex + (getScanline - splitInfo->ringBufferFirstScanline)) %
      info->ringBufferNumEntries;
  return GetRingBufferEntry(info, splitInfo, ringBufferIndex);
}

static void DecodeScanline(ResizeInfo const* info, int n, float* outputBuffer) {
  int channels = info->channels;
  int effectiveChannels = info->effectiveChannels;
  int inputSampleInBytes = TypeSize[(size_t)info->inputType] * channels;
  int row = EdgeWrap(n, info->vertical.scaleInfo.inputFullSize);
  const void* inputPlaneData =
      ((char*)info->inputData) + (size_t)row * (size_t)info->inputStrideBytes;
  Span const* spans = info->scanlineExtents.spans;
  float* fullDecodeBuffer =
      outputBuffer - info->scanlineExtents.conservative.n0 * effectiveChannels;
  float* lastDecoded = 0;

  assert(!(n < 0 || n >= info->vertical.scaleInfo.inputFullSize));

  do {
    float* decodeBuffer;
    void const* inputData;
    float* endDecode;
    int widthTimesChannels;
    int width;

    if (spans->n1 < spans->n0) break;

    width = spans->n1 + 1 - spans->n0;
    decodeBuffer = fullDecodeBuffer + spans->n0 * effectiveChannels;
    endDecode = fullDecodeBuffer + (spans->n1 + 1) * effectiveChannels;
    widthTimesChannels = width * channels;

    // read directly out of input plane by default
    inputData = ((char*)inputPlaneData) + spans->pixelOffsetForInput * inputSampleInBytes;

    // convert the pixels info the float decodeBuffer, (we index from endDecode, so that when channels<effectiveChannels, we are right justified in the buffer)
    lastDecoded =
        info->decodePixels((float*)endDecode - widthTimesChannels, widthTimesChannels, inputData);

    if (info->alphaWeight) {
      info->alphaWeight(decodeBuffer, widthTimesChannels);
    }

    ++spans;
  } while (spans <= (&info->scanlineExtents.spans[1]));

  // some of the horizontal gathers read one float off the edge (which is masked out), but we force a zero here to make sure no NaNs leak in
  //   (we can't pre-zero it, because the input callback can use that area as padding)
  lastDecoded[0] = 0.0f;

  // we clear this extra float, because the final output pixel filter kernel might have used one less coeff than the max filter width
  //   when this happens, we do read that pixel from the input, so it too could be Nan, so just zero an extra one.
  //   this fits because each scanline is padded by three floats (TGFX_INPUT_CALLBACK_PADDING)
  lastDecoded[1] = 0.0f;
}

static void ResampleHorizontalGather(ResizeInfo const* info, float* outputBuffer,
                                     float const* inputBuffer) {
  float const* decodeBuffer =
      inputBuffer - (info->scanlineExtents.conservative.n0 * info->effectiveChannels);
  info->horizontalGatherchannels(outputBuffer,
                                 (unsigned int)info->horizontal.scaleInfo.outputSubSize,
                                 decodeBuffer, info->horizontal.contributors,
                                 info->horizontal.coefficients, info->horizontal.coefficientWidth);
}

static void DecodeAndResampleForVerticalGatherLoop(ResizeInfo const* info, PerSplitInfo* splitInfo,
                                                   int n) {
  int ringBufferIndex;
  float* ringBuffer;

  // Decode the nth scanline from the source image into the decode buffer.
  DecodeScanline(info, n, splitInfo->decodeBuffer);

  // update new end scanline
  splitInfo->ringBufferLastScanline = n;

  // get ring buffer
  ringBufferIndex = (splitInfo->ringBufferBeginIndex +
                     (splitInfo->ringBufferLastScanline - splitInfo->ringBufferFirstScanline)) %
                    info->ringBufferNumEntries;
  ringBuffer = GetRingBufferEntry(info, splitInfo, ringBufferIndex);

  // Now resample it into the ring buffer.
  ResampleHorizontalGather(info, ringBuffer, splitInfo->decodeBuffer);

  // Now it's sitting in the ring buffer ready to be used as source for the vertical sampling.
}

typedef void VerticalGatherFunc(float* output, float const* coeffs, float const** inputs,
                                float const* input0End);
static VerticalGatherFunc* VerticalGathers[8] = {
    VerticalGatherWith1Coeffs, VerticalGatherWith2Coeffs, VerticalGatherWith3Coeffs,
    VerticalGatherWith4Coeffs, VerticalGatherWith5Coeffs, VerticalGatherWith6Coeffs,
    VerticalGatherWith7Coeffs, VerticalGatherWith8Coeffs};

static VerticalGatherFunc* VerticalGathersContinues[8] = {
    VerticalGatherWith1CoeffsCont, VerticalGatherWith2CoeffsCont, VerticalGatherWith3CoeffsCont,
    VerticalGatherWith4CoeffsCont, VerticalGatherWith5CoeffsCont, VerticalGatherWith6CoeffsCont,
    VerticalGatherWith7CoeffsCont, VerticalGatherWith8CoeffsCont};

static void EncodeScanline(ResizeInfo const* info, void* outputBufferData, float* encodeBuffer) {
  int numPixels = info->horizontal.scaleInfo.outputSubSize;
  int channels = info->channels;
  int widthTimesChannels = numPixels * channels;
  void* outputBuffer;

  // un-alpha weight if we need to
  if (info->alphaUnweight) {
    info->alphaUnweight(encodeBuffer, widthTimesChannels);
  }

  // write directly into output by default
  outputBuffer = outputBufferData;

  // convert into the output buffer
  info->encodePixels(outputBuffer, widthTimesChannels, encodeBuffer);
}

static void ResampleVerticalGather(ResizeInfo const* info, PerSplitInfo* splitInfo, int n,
                                   int contribN0, int contribN1,
                                   float const* verticalCoefficients) {
  float* encodeBuffer = splitInfo->verticalBuffer;
  float* decodeBuffer = splitInfo->decodeBuffer;
  int verticalFirst = info->verticalFirst;
  int width =
      (verticalFirst)
          ? (info->scanlineExtents.conservative.n1 - info->scanlineExtents.conservative.n0 + 1)
          : info->horizontal.scaleInfo.outputSubSize;
  int widthTimesChannels = info->effectiveChannels * width;

  assert(info->vertical.isGather);

  // loop over the contributing scanlines and scale into the buffer
  {
    int k = 0, total = contribN1 - contribN0 + 1;
    assert(total > 0);
    do {
      float const* inputs[8];
      int i, cnt = total;
      if (cnt > 8) cnt = 8;
      for (i = 0; i < cnt; i++)
        inputs[i] = GetRingbufferScanline(info, splitInfo, k + i + contribN0);

      // call the N scanlines at a time function (up to 8 scanlines of blending at once)
      ((k == 0) ? VerticalGathers : VerticalGathersContinues)[cnt - 1](
          (verticalFirst) ? decodeBuffer : encodeBuffer, verticalCoefficients + k, inputs,
          inputs[0] + widthTimesChannels);
      k += cnt;
      total -= cnt;
    } while (total);
  }

  if (verticalFirst) {
    // Now resample the gathered vertical data in the horizontal axis into the encode buffer
    decodeBuffer[widthTimesChannels] = 0.0f;  // clear two over for horizontals with a remnant of 3
    decodeBuffer[widthTimesChannels + 1] = 0.0f;
    ResampleHorizontalGather(info, encodeBuffer, decodeBuffer);
  }

  EncodeScanline(info, ((char*)info->outputData) + ((size_t)n * (size_t)info->outputStrideBytes),
                 encodeBuffer);
}

static void VerticalGatherLoop(ResizeInfo const* info, PerSplitInfo* splitInfo, int splitCount) {
  int y, startOutputY, endOutputY;
  Contributors* verticalContributors = info->vertical.contributors;
  float const* verticalCoefficients = info->vertical.coefficients;

  assert(info->vertical.isGather);

  startOutputY = splitInfo->startOutputY;
  endOutputY = splitInfo[splitCount - 1].endOutputY;

  verticalContributors += startOutputY;
  verticalCoefficients += startOutputY * info->vertical.coefficientWidth;

  // initialize the ring buffer for gathering
  splitInfo->ringBufferBeginIndex = 0;
  splitInfo->ringBufferFirstScanline = verticalContributors->n0;
  splitInfo->ringBufferLastScanline = splitInfo->ringBufferFirstScanline - 1;  // means "empty"

  for (y = startOutputY; y < endOutputY; y++) {
    int inFirstScanline, inLastScanline;

    inFirstScanline = verticalContributors->n0;
    inLastScanline = verticalContributors->n1;

    // make sure the indexing hasn't broken
    assert(inFirstScanline >= splitInfo->ringBufferFirstScanline);

    // Load in new scanlines
    while (inLastScanline > splitInfo->ringBufferLastScanline) {
      assert((splitInfo->ringBufferLastScanline - splitInfo->ringBufferFirstScanline + 1) <=
             info->ringBufferNumEntries);

      // make sure there was room in the ring buffer when we add new scanlines
      if ((splitInfo->ringBufferLastScanline - splitInfo->ringBufferFirstScanline + 1) ==
          info->ringBufferNumEntries) {
        splitInfo->ringBufferFirstScanline++;
        splitInfo->ringBufferBeginIndex++;
      }

      if (info->verticalFirst) {
        float* ringBuffer =
            GetRingbufferScanline(info, splitInfo, ++splitInfo->ringBufferLastScanline);
        // Decode the nth scanline from the source image into the decode buffer.
        DecodeScanline(info, splitInfo->ringBufferLastScanline, ringBuffer);
      } else {
        DecodeAndResampleForVerticalGatherLoop(info, splitInfo,
                                               splitInfo->ringBufferLastScanline + 1);
      }
    }

    // Now all buffers should be ready to write a row of vertical sampling, so do it.
    ResampleVerticalGather(info, splitInfo, y, inFirstScanline, inLastScanline,
                           verticalCoefficients);

    ++verticalContributors;
    verticalCoefficients += info->vertical.coefficientWidth;
  }
}

#define TGFX_FLOAT_EMPTY_MARKER 3.0e+38F
#define TGFX_FLOAT_BUFFER_IS_EMPTY(ptr) ((ptr)[0] == TGFX_FLOAT_EMPTY_MARKER)
static void HorizontalResampleAndEncodeFirstScanlineFromScatter(ResizeInfo const* info,
                                                                PerSplitInfo* splitInfo) {
  // evict a scanline out into the output buffer

  float* ringBufferEntry = GetRingBufferEntry(info, splitInfo, splitInfo->ringBufferBeginIndex);

  // Now resample it into the buffer.
  ResampleHorizontalGather(info, splitInfo->verticalBuffer, ringBufferEntry);

  // dump the scanline out
  EncodeScanline(info,
                 ((char*)info->outputData) +
                     ((size_t)splitInfo->ringBufferFirstScanline * (size_t)info->outputStrideBytes),
                 splitInfo->verticalBuffer);

  // mark it as empty
  ringBufferEntry[0] = TGFX_FLOAT_EMPTY_MARKER;

  // advance the first scanline
  splitInfo->ringBufferFirstScanline++;
  if (++splitInfo->ringBufferBeginIndex == info->ringBufferNumEntries)
    splitInfo->ringBufferBeginIndex = 0;
}

static void EncodeFirstScanlineFromScatter(ResizeInfo const* info, PerSplitInfo* splitInfo) {
  // evict a scanline out into the output buffer
  float* ringBufferEntry = GetRingBufferEntry(info, splitInfo, splitInfo->ringBufferBeginIndex);

  // dump the scanline out
  EncodeScanline(info,
                 ((char*)info->outputData) +
                     ((size_t)splitInfo->ringBufferFirstScanline * (size_t)info->outputStrideBytes),
                 ringBufferEntry);

  // mark it as empty
  ringBufferEntry[0] = TGFX_FLOAT_EMPTY_MARKER;

  // advance the first scanline
  splitInfo->ringBufferFirstScanline++;
  if (++splitInfo->ringBufferBeginIndex == info->ringBufferNumEntries)
    splitInfo->ringBufferBeginIndex = 0;
}

typedef void VerticalScatterFunc(float** outputs, float const* coeffs, float const* input,
                                 float const* inputEnd);
static VerticalScatterFunc* VerticalScatterSets[8] = {
    VerticalScatterWith1Coeffs, VerticalScatterWith2Coeffs, VerticalScatterWith3Coeffs,
    VerticalScatterWith4Coeffs, VerticalScatterWith5Coeffs, VerticalScatterWith6Coeffs,
    VerticalScatterWith7Coeffs, VerticalScatterWith8Coeffs};

static VerticalScatterFunc* VerticalScatterBlends[8] = {
    VerticalScatterWith1CoeffsCont, VerticalScatterWith2CoeffsCont, VerticalScatterWith3CoeffsCont,
    VerticalScatterWith4CoeffsCont, VerticalScatterWith5CoeffsCont, VerticalScatterWith6CoeffsCont,
    VerticalScatterWith7CoeffsCont, VerticalScatterWith8CoeffsCont};

static void ResampleVerticalScatter(ResizeInfo const* info, PerSplitInfo* splitInfo, int n0, int n1,
                                    float const* verticalCoefficients, float const* verticalBuffer,
                                    float const* verticalBufferEnd) {
  assert(!info->vertical.isGather);

  {
    int k = 0, total = n1 - n0 + 1;
    assert(total > 0);
    do {
      float* outputs[8];
      int i, n = total;
      if (n > 8) n = 8;
      for (i = 0; i < n; i++) {
        outputs[i] = GetRingbufferScanline(info, splitInfo, k + i + n0);
        if ((i) && (TGFX_FLOAT_BUFFER_IS_EMPTY(outputs[i]) !=
                    TGFX_FLOAT_BUFFER_IS_EMPTY(outputs[0])))  // make sure runs are of the same type
        {
          n = i;
          break;
        }
      }
      // call the scatter to N scanlines at a time function (up to 8 scanlines of scattering at once)
      ((TGFX_FLOAT_BUFFER_IS_EMPTY(outputs[0])) ? VerticalScatterSets
                                                : VerticalScatterBlends)[n - 1](
          outputs, verticalCoefficients + k, verticalBuffer, verticalBufferEnd);
      k += n;
      total -= n;
    } while (total);
  }
}

typedef void HandleScanlineForScatterFunc(ResizeInfo const* info, PerSplitInfo* splitInfo);
static void VerticalScatterLoop(ResizeInfo const* info, PerSplitInfo* splitInfo, int splitCount) {
  int y, startOutputY, endOutputY, startInputY, endInputY;
  Contributors* verticalContributors = info->vertical.contributors;
  float const* verticalCoefficients = info->vertical.coefficients;
  HandleScanlineForScatterFunc* handleScanlineForScatter;
  void* scanlineScatterBuffer;
  void* scanlineScatterBufferEnd;
  int onFirstInputY, lastInputY;
  int width =
      (info->verticalFirst)
          ? (info->scanlineExtents.conservative.n1 - info->scanlineExtents.conservative.n0 + 1)
          : info->horizontal.scaleInfo.outputSubSize;
  int widthTimesChannels = info->effectiveChannels * width;

  assert(!info->vertical.isGather);

  startOutputY = splitInfo->startOutputY;
  endOutputY = splitInfo[splitCount - 1].endOutputY;  // may do multiple split counts

  startInputY = splitInfo->startInputY;
  endInputY = splitInfo[splitCount - 1].endInputY;

  // adjust for starting offset startInputY
  y = startInputY + info->vertical.filterPixelMargin;
  verticalContributors += y;
  verticalCoefficients += info->vertical.coefficientWidth * y;

  if (info->verticalFirst) {
    handleScanlineForScatter = HorizontalResampleAndEncodeFirstScanlineFromScatter;
    scanlineScatterBuffer = splitInfo->decodeBuffer;
    scanlineScatterBufferEnd = ((char*)scanlineScatterBuffer) +
                               sizeof(float) * (unsigned long)info->effectiveChannels *
                                   (unsigned long)(info->scanlineExtents.conservative.n1 -
                                                   info->scanlineExtents.conservative.n0 + 1);
  } else {
    handleScanlineForScatter = EncodeFirstScanlineFromScatter;
    scanlineScatterBuffer = splitInfo->verticalBuffer;
    scanlineScatterBufferEnd = ((char*)scanlineScatterBuffer) +
                               sizeof(float) * (unsigned long)info->effectiveChannels *
                                   (unsigned long)info->horizontal.scaleInfo.outputSubSize;
  }

  // initialize the ring buffer for scattering
  splitInfo->ringBufferFirstScanline = startOutputY;
  splitInfo->ringBufferLastScanline = -1;
  splitInfo->ringBufferBeginIndex = -1;

  // mark all the buffers as empty to start
  for (y = 0; y < info->ringBufferNumEntries; y++) {
    float* decodeBuffer = GetRingBufferEntry(info, splitInfo, y);
    decodeBuffer[widthTimesChannels] = 0.0f;  // clear two over for horizontals with a remnant of 3
    decodeBuffer[widthTimesChannels + 1] = 0.0f;
    decodeBuffer[0] = TGFX_FLOAT_EMPTY_MARKER;  // only used on scatter
  }

  // do the loop in input space
  onFirstInputY = 1;
  lastInputY = startInputY;
  for (y = startInputY; y < endInputY; y++) {
    int outFirstScanline, outLastScanline;

    outFirstScanline = verticalContributors->n0;
    outLastScanline = verticalContributors->n1;

    assert(outLastScanline - outFirstScanline + 1 <= info->ringBufferNumEntries);

    if ((outLastScanline >= outFirstScanline) &&
        (((outFirstScanline >= startOutputY) && (outFirstScanline < endOutputY)) ||
         ((outLastScanline >= startOutputY) && (outLastScanline < endOutputY)))) {
      float const* vc = verticalCoefficients;

      // keep track of the range actually seen for the next resize
      lastInputY = y;
      if ((onFirstInputY) && (y > startInputY)) splitInfo->startInputY = y;
      onFirstInputY = 0;

      // clip the region
      if (outFirstScanline < startOutputY) {
        vc += startOutputY - outFirstScanline;
        outFirstScanline = startOutputY;
      }

      if (outLastScanline >= endOutputY) outLastScanline = endOutputY - 1;

      // if very first scanline, init the index
      if (splitInfo->ringBufferBeginIndex < 0)
        splitInfo->ringBufferBeginIndex = outFirstScanline - startOutputY;

      assert(splitInfo->ringBufferBeginIndex <= outFirstScanline);

      // Decode the nth scanline from the source image into the decode buffer.
      DecodeScanline(info, y, splitInfo->decodeBuffer);

      // When horizontal first, we resample horizontally into the vertical buffer before we scatter it out
      if (!info->verticalFirst)
        ResampleHorizontalGather(info, splitInfo->verticalBuffer, splitInfo->decodeBuffer);

      // Now it's sitting in the buffer ready to be distributed into the ring buffers.

      // evict from the ringbuffer, if we need are full
      if (((splitInfo->ringBufferLastScanline - splitInfo->ringBufferFirstScanline + 1) ==
           info->ringBufferNumEntries) &&
          (outLastScanline > splitInfo->ringBufferLastScanline))
        handleScanlineForScatter(info, splitInfo);

      // Now the horizontal buffer is ready to write to all ring buffer rows, so do it.
      ResampleVerticalScatter(info, splitInfo, outFirstScanline, outLastScanline, vc,
                              (float*)scanlineScatterBuffer, (float*)scanlineScatterBufferEnd);

      // update the end of the buffer
      if (outLastScanline > splitInfo->ringBufferLastScanline)
        splitInfo->ringBufferLastScanline = outLastScanline;
    }
    ++verticalContributors;
    verticalCoefficients += info->vertical.coefficientWidth;
  }

  // now evict the scanlines that are left over in the ring buffer
  while (splitInfo->ringBufferFirstScanline < endOutputY) handleScanlineForScatter(info, splitInfo);

  // update the endInputY if we do multiple resizes with the same data
  ++lastInputY;
  for (y = 0; y < splitCount; y++)
    if (splitInfo[y].endInputY > lastInputY) splitInfo[y].endInputY = lastInputY;
}

static int PerformResize(ResizeInfo const* info, int splitStart, int splitCount) {
  PerSplitInfo* splitInfo = info->splitInfo + splitStart;

  if (info->vertical.isGather) VerticalGatherLoop(info, splitInfo, splitCount);
  else
    VerticalScatterLoop(info, splitInfo, splitCount);

  return 1;
}

static int ResizeExtended(ResizeData* resize) {
  int result;
  // remember allocated state
  int allocState = resize->calledAlloc;

  if (!BuildSamplers(resize)) return 0;

  resize->calledAlloc = allocState;

  // if build_samplers succeeded (above), but there are no samplers set, then
  //   the area to stretch into was zero pixels, so don't do anything and return
  //   success
  if (resize->samplers == 0) return 1;

  // do resize
  result = PerformResize(resize->samplers, 0, resize->splits);

  // if we alloced, then free
  if (!resize->calledAlloc) {
    FreeSamplers(resize);
    resize->samplers = 0;
  }

  return result;
}

bool ImageResize(const void* inputPixels, int inputW, int inputH, int inputStrideInBytes,
                 void* outputPixels, int outputW, int outputH, int outputStrideInBytes,
                 PixelLayout pixelLayout, DataType dataType) {
  ResizeData resize;
  if (!CheckOutputStuff(outputPixels, TypeSize[static_cast<size_t>(dataType)], outputW, outputH,
                        outputStrideInBytes,
                        PixelLayoutConvertPublicToInternal[static_cast<size_t>(pixelLayout)])) {
    return false;
  }
  ResizeInit(&resize, inputPixels, inputW, inputH, inputStrideInBytes, outputPixels, outputW,
             outputH, outputStrideInBytes, pixelLayout, dataType);

  if (!ResizeExtended(&resize)) {
    return false;
  }
  return true;
}
}  // namespace tgfx
#else

// we reinclude the header file to define all the horizontal functions
//   specializing each function for the number of coeffs is 20-40% faster *OVERALL*

// by including the source file again this way, we can still debug the functions

#define TGFX_STR_JOIN2(start, mid, end) start##mid##end
#define TGFX_STR_JOIN1(start, mid, end) TGFX_STR_JOIN2(start, mid, end)

#define TGFX_STRS_JOIN24(start, mid1, mid2, end) start##mid1##mid2##end
#define TGFX_STRS_JOIN14(start, mid1, mid2, end) TGFX_STRS_JOIN24(start, mid1, mid2, end)

#ifdef TGFX_IMAGE_RESIZE_DO_CODERS
#ifdef TGFX_DECODE_SUFFIX
#define TGFX_CODER_NAME(name) TGFX_STR_JOIN1(name, , TGFX_DECODE_SUFFIX)
#else
#define TGFX_CODER_NAME(name) name
#endif

#ifndef TGFX_DECODE_SWIZZLE
#define TGFX_DECODE_ORDER0 0
#define TGFX_DECODE_ORDER1 1
#define TGFX_DECODE_ORDER2 2
#define TGFX_DECODE_ORDER3 3
#define TGFX_ENCODE_ORDER0 0
#define TGFX_ENCODE_ORDER1 1
#define TGFX_ENCODE_ORDER2 2
#define TGFX_ENCODE_ORDER3 3
#endif

static float* TGFX_CODER_NAME(DecodeUint8LinearScaled)(float* decodep, int widthTimesChannels,
                                                       void const* inputp) {
  float TGFX_STREAMOUT_PTR(*) decode = decodep;
  float* decodeEnd = (float*)decode + widthTimesChannels;
  unsigned char const* input = (unsigned char const*)inputp;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  decode += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (decode <= decodeEnd) {
    TGFX_SIMD_NO_UNROLL(decode);
    decode[0 - 4] = ((float)(input[TGFX_DECODE_ORDER0])) * MAX_UINT8_AS_FLOAT_INVERTED;
    decode[1 - 4] = ((float)(input[TGFX_DECODE_ORDER1])) * MAX_UINT8_AS_FLOAT_INVERTED;
    decode[2 - 4] = ((float)(input[TGFX_DECODE_ORDER2])) * MAX_UINT8_AS_FLOAT_INVERTED;
    decode[3 - 4] = ((float)(input[TGFX_DECODE_ORDER3])) * MAX_UINT8_AS_FLOAT_INVERTED;
    decode += 4;
    input += 4;
  }
  decode -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (decode < decodeEnd) {
    TGFX_NO_UNROLL(decode);
    decode[0] = ((float)(input[TGFX_DECODE_ORDER0])) * MAX_UINT8_AS_FLOAT_INVERTED;
#if TGFX_CODER_MIN_NUM >= 2
    decode[1] = ((float)(input[TGFX_DECODE_ORDER1])) * MAX_UINT8_AS_FLOAT_INVERTED;
#endif
#if TGFX_CODER_MIN_NUM >= 3
    decode[2] = ((float)(input[TGFX_DECODE_ORDER2])) * MAX_UINT8_AS_FLOAT_INVERTED;
#endif
    decode += TGFX_CODER_MIN_NUM;
    input += TGFX_CODER_MIN_NUM;
  }
#endif

  return decodeEnd;
}

static void TGFX_CODER_NAME(EncodeUint8LinearScaled)(void* outputp, int widthTimesChannels,
                                                     float const* encode) {
  unsigned char TGFX_SIMD_STREAMOUT_PTR(*) output = (unsigned char*)outputp;
  unsigned char* endOutput = ((unsigned char*)output) + widthTimesChannels;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  output += 4;
  while (output <= endOutput) {
    float f;
    f = encode[TGFX_ENCODE_ORDER0] * MAX_UINT8_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[0 - 4] = (unsigned char)f;
    f = encode[TGFX_ENCODE_ORDER1] * MAX_UINT8_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[1 - 4] = (unsigned char)f;
    f = encode[TGFX_ENCODE_ORDER2] * MAX_UINT8_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[2 - 4] = (unsigned char)f;
    f = encode[TGFX_ENCODE_ORDER3] * MAX_UINT8_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[3 - 4] = (unsigned char)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (output < endOutput) {
    float f;
    TGFX_NO_UNROLL(encode);
    f = encode[TGFX_ENCODE_ORDER0] * MAX_UINT8_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[0] = (unsigned char)f;
#if TGFX_CODER_MIN_NUM >= 2
    f = encode[TGFX_ENCODE_ORDER1] * MAX_UINT8_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[1] = (unsigned char)f;
#endif
#if TGFX_CODER_MIN_NUM >= 3
    f = encode[TGFX_ENCODE_ORDER2] * MAX_UINT8_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[2] = (unsigned char)f;
#endif
    output += TGFX_CODER_MIN_NUM;
    encode += TGFX_CODER_MIN_NUM;
  }
#endif
}

static float* TGFX_CODER_NAME(DecodeUint8Linear)(float* decodep, int widthTimesChannels,
                                                 void const* inputp) {
  float TGFX_STREAMOUT_PTR(*) decode = decodep;
  float* decodeEnd = (float*)decode + widthTimesChannels;
  unsigned char const* input = (unsigned char const*)inputp;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  decode += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (decode <= decodeEnd) {
    TGFX_SIMD_NO_UNROLL(decode);
    decode[0 - 4] = ((float)(input[TGFX_DECODE_ORDER0]));
    decode[1 - 4] = ((float)(input[TGFX_DECODE_ORDER1]));
    decode[2 - 4] = ((float)(input[TGFX_DECODE_ORDER2]));
    decode[3 - 4] = ((float)(input[TGFX_DECODE_ORDER3]));
    decode += 4;
    input += 4;
  }
  decode -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (decode < decodeEnd) {
    TGFX_NO_UNROLL(decode);
    decode[0] = ((float)(input[TGFX_DECODE_ORDER0]));
#if TGFX_CODER_MIN_NUM >= 2
    decode[1] = ((float)(input[TGFX_DECODE_ORDER1]));
#endif
#if TGFX_CODER_MIN_NUM >= 3
    decode[2] = ((float)(input[TGFX_DECODE_ORDER2]));
#endif
    decode += TGFX_CODER_MIN_NUM;
    input += TGFX_CODER_MIN_NUM;
  }
#endif
  return decodeEnd;
}

static void TGFX_CODER_NAME(EncodeUint8Linear)(void* outputp, int widthTimesChannels,
                                               float const* encode) {
  unsigned char TGFX_SIMD_STREAMOUT_PTR(*) output = (unsigned char*)outputp;
  unsigned char* endOutput = ((unsigned char*)output) + widthTimesChannels;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  output += 4;
  while (output <= endOutput) {
    float f;
    f = encode[TGFX_ENCODE_ORDER0] + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[0 - 4] = (unsigned char)f;
    f = encode[TGFX_ENCODE_ORDER1] + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[1 - 4] = (unsigned char)f;
    f = encode[TGFX_ENCODE_ORDER2] + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[2 - 4] = (unsigned char)f;
    f = encode[TGFX_ENCODE_ORDER3] + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[3 - 4] = (unsigned char)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (output < endOutput) {
    float f;
    TGFX_NO_UNROLL(encode);
    f = encode[TGFX_ENCODE_ORDER0] + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[0] = (unsigned char)f;
#if TGFX_CODER_MIN_NUM >= 2
    f = encode[TGFX_ENCODE_ORDER1] + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[1] = (unsigned char)f;
#endif
#if TGFX_CODER_MIN_NUM >= 3
    f = encode[TGFX_ENCODE_ORDER2] + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[2] = (unsigned char)f;
#endif
    output += TGFX_CODER_MIN_NUM;
    encode += TGFX_CODER_MIN_NUM;
  }
#endif
}

static float* TGFX_CODER_NAME(DecodeUint8SRGB)(float* decodep, int widthTimesChannels,
                                               void const* inputp) {
  float TGFX_STREAMOUT_PTR(*) decode = decodep;
  float* decodeEnd = (float*)decode + widthTimesChannels;
  unsigned char const* input = (unsigned char const*)inputp;

  // try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  decode += 4;
  while (decode <= decodeEnd) {
    decode[0 - 4] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER0]];
    decode[1 - 4] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER1]];
    decode[2 - 4] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER2]];
    decode[3 - 4] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER3]];
    decode += 4;
    input += 4;
  }
  decode -= 4;
#endif

  // do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (decode < decodeEnd) {
    TGFX_NO_UNROLL(decode);
    decode[0] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER0]];
#if TGFX_CODER_MIN_NUM >= 2
    decode[1] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER1]];
#endif
#if TGFX_CODER_MIN_NUM >= 3
    decode[2] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER2]];
#endif
    decode += TGFX_CODER_MIN_NUM;
    input += TGFX_CODER_MIN_NUM;
  }
#endif
  return decodeEnd;
}

static void TGFX_CODER_NAME(EncodeUint8SRGB)(void* outputp, int widthTimesChannels,
                                             float const* encode) {
  unsigned char TGFX_SIMD_STREAMOUT_PTR(*) output = (unsigned char*)outputp;
  unsigned char* endOutput = ((unsigned char*)output) + widthTimesChannels;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  output += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (output <= endOutput) {
    TGFX_SIMD_NO_UNROLL(encode);

    output[0 - 4] = LinearToSRGBUchar(encode[TGFX_ENCODE_ORDER0]);
    output[1 - 4] = LinearToSRGBUchar(encode[TGFX_ENCODE_ORDER1]);
    output[2 - 4] = LinearToSRGBUchar(encode[TGFX_ENCODE_ORDER2]);
    output[3 - 4] = LinearToSRGBUchar(encode[TGFX_ENCODE_ORDER3]);

    output += 4;
    encode += 4;
  }
  output -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (output < endOutput) {
    TGFX_NO_UNROLL(encode);
    output[0] = LinearToSRGBUchar(encode[TGFX_ENCODE_ORDER0]);
#if TGFX_CODER_MIN_NUM >= 2
    output[1] = LinearToSRGBUchar(encode[TGFX_ENCODE_ORDER1]);
#endif
#if TGFX_CODER_MIN_NUM >= 3
    output[2] = LinearToSRGBUchar(encode[TGFX_ENCODE_ORDER2]);
#endif
    output += TGFX_CODER_MIN_NUM;
    encode += TGFX_CODER_MIN_NUM;
  }
#endif
}

#if (TGFX_CODER_MIN_NUM == 4) || ((TGFX_CODER_MIN_NUM == 1) && (!defined(TGFX_DECODE_SWIZZLE)))

static float* TGFX_CODER_NAME(DecodeUint8SRGB4LinearAlpha)(float* decodep, int widthTimesChannels,
                                                           void const* inputp) {
  float TGFX_STREAMOUT_PTR(*) decode = decodep;
  float* decodeEnd = (float*)decode + widthTimesChannels;
  unsigned char const* input = (unsigned char const*)inputp;

  do {
    decode[0] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER0]];
    decode[1] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER1]];
    decode[2] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER2]];
    decode[3] = ((float)input[TGFX_DECODE_ORDER3]) * MAX_UINT8_AS_FLOAT_INVERTED;
    input += 4;
    decode += 4;
  } while (decode < decodeEnd);
  return decodeEnd;
}

static void TGFX_CODER_NAME(EncodeUint8SRGB4LinearAlpha)(void* outputp, int widthTimesChannels,
                                                         float const* encode) {
  unsigned char TGFX_SIMD_STREAMOUT_PTR(*) output = (unsigned char*)outputp;
  unsigned char* endOutput = ((unsigned char*)output) + widthTimesChannels;

  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float f;
    TGFX_SIMD_NO_UNROLL(encode);

    output[TGFX_DECODE_ORDER0] = LinearToSRGBUchar(encode[0]);
    output[TGFX_DECODE_ORDER1] = LinearToSRGBUchar(encode[1]);
    output[TGFX_DECODE_ORDER2] = LinearToSRGBUchar(encode[2]);

    f = encode[3] * MAX_UINT8_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[TGFX_DECODE_ORDER3] = (unsigned char)f;

    output += 4;
    encode += 4;
  } while (output < endOutput);
}
#endif

#if (TGFX_CODER_MIN_NUM == 2) || ((TGFX_CODER_MIN_NUM == 1) && (!defined(TGFX_DECODE_SWIZZLE)))

static float* TGFX_CODER_NAME(DecodeUint8SRGB2LinearAlpha)(float* decodep, int widthTimesChannels,
                                                           void const* inputp) {
  float TGFX_STREAMOUT_PTR(*) decode = decodep;
  float* decodeEnd = (float*)decode + widthTimesChannels;
  unsigned char const* input = (unsigned char const*)inputp;

  decode += 4;
  while (decode <= decodeEnd) {
    decode[0 - 4] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER0]];
    decode[1 - 4] = ((float)input[TGFX_DECODE_ORDER1]) * MAX_UINT8_AS_FLOAT_INVERTED;
    decode[2 - 4] = SRGBUcharToLinearFloat[input[TGFX_DECODE_ORDER0 + 2]];
    decode[3 - 4] = ((float)input[TGFX_DECODE_ORDER1 + 2]) * MAX_UINT8_AS_FLOAT_INVERTED;
    input += 4;
    decode += 4;
  }
  decode -= 4;
  if (decode < decodeEnd) {
    decode[0] = SRGBUcharToLinearFloat[TGFX_DECODE_ORDER0];
    decode[1] = ((float)input[TGFX_DECODE_ORDER1]) * MAX_UINT8_AS_FLOAT_INVERTED;
  }
  return decodeEnd;
}

static void TGFX_CODER_NAME(EncodeUint8SRGB2LinearAlpha)(void* outputp, int widthTimesChannels,
                                                         float const* encode) {
  unsigned char TGFX_SIMD_STREAMOUT_PTR(*) output = (unsigned char*)outputp;
  unsigned char* endOutput = ((unsigned char*)output) + widthTimesChannels;

  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float f;
    TGFX_SIMD_NO_UNROLL(encode);

    output[TGFX_DECODE_ORDER0] = LinearToSRGBUchar(encode[0]);

    f = encode[1] * MAX_UINT8_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 255);
    output[TGFX_DECODE_ORDER1] = (unsigned char)f;

    output += 2;
    encode += 2;
  } while (output < endOutput);
}

#endif

static float* TGFX_CODER_NAME(DecodeUint16LinearScaled)(float* decodep, int widthTimesChannels,
                                                        void const* inputp) {
  float TGFX_STREAMOUT_PTR(*) decode = decodep;
  float* decodeEnd = (float*)decode + widthTimesChannels;
  unsigned short const* input = (unsigned short const*)inputp;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  decode += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (decode <= decodeEnd) {
    TGFX_SIMD_NO_UNROLL(decode);
    decode[0 - 4] = ((float)(input[TGFX_DECODE_ORDER0])) * MAX_UINT16_AS_FLOAT_INVERTED;
    decode[1 - 4] = ((float)(input[TGFX_DECODE_ORDER1])) * MAX_UINT16_AS_FLOAT_INVERTED;
    decode[2 - 4] = ((float)(input[TGFX_DECODE_ORDER2])) * MAX_UINT16_AS_FLOAT_INVERTED;
    decode[3 - 4] = ((float)(input[TGFX_DECODE_ORDER3])) * MAX_UINT16_AS_FLOAT_INVERTED;
    decode += 4;
    input += 4;
  }
  decode -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (decode < decodeEnd) {
    TGFX_NO_UNROLL(decode);
    decode[0] = ((float)(input[TGFX_DECODE_ORDER0])) * MAX_UINT16_AS_FLOAT_INVERTED;
#if TGFX_CODER_MIN_NUM >= 2
    decode[1] = ((float)(input[TGFX_DECODE_ORDER1])) * MAX_UINT16_AS_FLOAT_INVERTED;
#endif
#if TGFX_CODER_MIN_NUM >= 3
    decode[2] = ((float)(input[TGFX_DECODE_ORDER2])) * MAX_UINT16_AS_FLOAT_INVERTED;
#endif
    decode += TGFX_CODER_MIN_NUM;
    input += TGFX_CODER_MIN_NUM;
  }
#endif
  return decodeEnd;
}

static void TGFX_CODER_NAME(EncodeUint16LinearScaled)(void* outputp, int widthTimesChannels,
                                                      float const* encode) {
  unsigned short TGFX_SIMD_STREAMOUT_PTR(*) output = (unsigned short*)outputp;
  unsigned short* endOutput = ((unsigned short*)output) + widthTimesChannels;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  output += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (output <= endOutput) {
    float f;
    TGFX_SIMD_NO_UNROLL(encode);
    f = encode[TGFX_ENCODE_ORDER0] * MAX_UINT16_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[0 - 4] = (unsigned short)f;
    f = encode[TGFX_ENCODE_ORDER1] * MAX_UINT16_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[1 - 4] = (unsigned short)f;
    f = encode[TGFX_ENCODE_ORDER2] * MAX_UINT16_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[2 - 4] = (unsigned short)f;
    f = encode[TGFX_ENCODE_ORDER3] * MAX_UINT16_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[3 - 4] = (unsigned short)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (output < endOutput) {
    float f;
    TGFX_NO_UNROLL(encode);
    f = encode[TGFX_ENCODE_ORDER0] * MAX_UINT16_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[0] = (unsigned short)f;
#if TGFX_CODER_MIN_NUM >= 2
    f = encode[TGFX_ENCODE_ORDER1] * MAX_UINT16_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[1] = (unsigned short)f;
#endif
#if TGFX_CODER_MIN_NUM >= 3
    f = encode[TGFX_ENCODE_ORDER2] * MAX_UINT16_AS_FLOAT + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[2] = (unsigned short)f;
#endif
    output += TGFX_CODER_MIN_NUM;
    encode += TGFX_CODER_MIN_NUM;
  }
#endif
}

static float* TGFX_CODER_NAME(DecodeUint16Linear)(float* decodep, int widthTimesChannels,
                                                  void const* inputp) {
  float TGFX_STREAMOUT_PTR(*) decode = decodep;
  float* decodeEnd = (float*)decode + widthTimesChannels;
  unsigned short const* input = (unsigned short const*)inputp;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  decode += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (decode <= decodeEnd) {
    TGFX_SIMD_NO_UNROLL(decode);
    decode[0 - 4] = ((float)(input[TGFX_DECODE_ORDER0]));
    decode[1 - 4] = ((float)(input[TGFX_DECODE_ORDER1]));
    decode[2 - 4] = ((float)(input[TGFX_DECODE_ORDER2]));
    decode[3 - 4] = ((float)(input[TGFX_DECODE_ORDER3]));
    decode += 4;
    input += 4;
  }
  decode -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (decode < decodeEnd) {
    TGFX_NO_UNROLL(decode);
    decode[0] = ((float)(input[TGFX_DECODE_ORDER0]));
#if TGFX_CODER_MIN_NUM >= 2
    decode[1] = ((float)(input[TGFX_DECODE_ORDER1]));
#endif
#if TGFX_CODER_MIN_NUM >= 3
    decode[2] = ((float)(input[TGFX_DECODE_ORDER2]));
#endif
    decode += TGFX_CODER_MIN_NUM;
    input += TGFX_CODER_MIN_NUM;
  }
#endif
  return decodeEnd;
}

static void TGFX_CODER_NAME(EncodeUint16Linear)(void* outputp, int widthTimesChannels,
                                                float const* encode) {
  unsigned short TGFX_SIMD_STREAMOUT_PTR(*) output = (unsigned short*)outputp;
  unsigned short* endOutput = ((unsigned short*)output) + widthTimesChannels;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  output += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (output <= endOutput) {
    float f;
    TGFX_SIMD_NO_UNROLL(encode);
    f = encode[TGFX_ENCODE_ORDER0] + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[0 - 4] = (unsigned short)f;
    f = encode[TGFX_ENCODE_ORDER1] + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[1 - 4] = (unsigned short)f;
    f = encode[TGFX_ENCODE_ORDER2] + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[2 - 4] = (unsigned short)f;
    f = encode[TGFX_ENCODE_ORDER3] + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[3 - 4] = (unsigned short)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (output < endOutput) {
    float f;
    TGFX_NO_UNROLL(encode);
    f = encode[TGFX_ENCODE_ORDER0] + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[0] = (unsigned short)f;
#if TGFX_CODER_MIN_NUM >= 2
    f = encode[TGFX_ENCODE_ORDER1] + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[1] = (unsigned short)f;
#endif
#if TGFX_CODER_MIN_NUM >= 3
    f = encode[TGFX_ENCODE_ORDER2] + 0.5f;
    TGFX_CLAMP(f, 0, 65535);
    output[2] = (unsigned short)f;
#endif
    output += TGFX_CODER_MIN_NUM;
    encode += TGFX_CODER_MIN_NUM;
  }
#endif
}

static float* TGFX_CODER_NAME(DecodeHalfFloatLinear)(float* decodep, int widthTimesChannels,
                                                     void const* inputp) {
  float TGFX_STREAMOUT_PTR(*) decode = decodep;
  float* decodeEnd = (float*)decode + widthTimesChannels;
  FP16 const* input = (FP16 const*)inputp;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  decode += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (decode <= decodeEnd) {
    TGFX_SIMD_NO_UNROLL(decode);
    decode[0 - 4] = HalfToFloat(input[TGFX_DECODE_ORDER0]);
    decode[1 - 4] = HalfToFloat(input[TGFX_DECODE_ORDER1]);
    decode[2 - 4] = HalfToFloat(input[TGFX_DECODE_ORDER2]);
    decode[3 - 4] = HalfToFloat(input[TGFX_DECODE_ORDER3]);
    decode += 4;
    input += 4;
  }
  decode -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (decode < decodeEnd) {
    TGFX_NO_UNROLL(decode);
    decode[0] = HalfToFloat(input[TGFX_DECODE_ORDER0]);
#if TGFX_CODER_MIN_NUM >= 2
    decode[1] = HalfToFloat(input[TGFX_DECODE_ORDER1]);
#endif
#if TGFX_CODER_MIN_NUM >= 3
    decode[2] = HalfToFloat(input[TGFX_DECODE_ORDER2]);
#endif
    decode += TGFX_CODER_MIN_NUM;
    input += TGFX_CODER_MIN_NUM;
  }
#endif
  return decodeEnd;
}

static void TGFX_CODER_NAME(EncodeHalfFloatLinear)(void* outputp, int widthTimesChannels,
                                                   float const* encode) {
  FP16 TGFX_SIMD_STREAMOUT_PTR(*) output = (FP16*)outputp;
  FP16* endOutput = ((FP16*)output) + widthTimesChannels;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  output += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (output <= endOutput) {
    TGFX_SIMD_NO_UNROLL(output);
    output[0 - 4] = FloatToHalf(encode[TGFX_ENCODE_ORDER0]);
    output[1 - 4] = FloatToHalf(encode[TGFX_ENCODE_ORDER1]);
    output[2 - 4] = FloatToHalf(encode[TGFX_ENCODE_ORDER2]);
    output[3 - 4] = FloatToHalf(encode[TGFX_ENCODE_ORDER3]);
    output += 4;
    encode += 4;
  }
  output -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (output < endOutput) {
    TGFX_NO_UNROLL(output);
    output[0] = FloatToHalf(encode[TGFX_ENCODE_ORDER0]);
#if TGFX_CODER_MIN_NUM >= 2
    output[1] = FloatToHalf(encode[TGFX_ENCODE_ORDER1]);
#endif
#if TGFX_CODER_MIN_NUM >= 3
    output[2] = FloatToHalf(encode[TGFX_ENCODE_ORDER2]);
#endif
    output += TGFX_CODER_MIN_NUM;
    encode += TGFX_CODER_MIN_NUM;
  }
#endif
}

static float* TGFX_CODER_NAME(DecodeFloatLinear)(float* decodep, int widthTimesChannels,
                                                 void const* inputp) {
#ifdef TGFX_DECODE_SWIZZLE
  float TGFX_STREAMOUT_PTR(*) decode = decodep;
  float* decodeEnd = (float*)decode + widthTimesChannels;
  float const* input = (float const*)inputp;

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  decode += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (decode <= decodeEnd) {
    TGFX_SIMD_NO_UNROLL(decode);
    decode[0 - 4] = input[TGFX_DECODE_ORDER0];
    decode[1 - 4] = input[TGFX_DECODE_ORDER1];
    decode[2 - 4] = input[TGFX_DECODE_ORDER2];
    decode[3 - 4] = input[TGFX_DECODE_ORDER3];
    decode += 4;
    input += 4;
  }
  decode -= 4;
#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (decode < decodeEnd) {
    TGFX_NO_UNROLL(decode);
    decode[0] = input[TGFX_DECODE_ORDER0];
#if TGFX_CODER_MIN_NUM >= 2
    decode[1] = input[TGFX_DECODE_ORDER1];
#endif
#if TGFX_CODER_MIN_NUM >= 3
    decode[2] = input[TGFX_DECODE_ORDER2];
#endif
    decode += TGFX_CODER_MIN_NUM;
    input += TGFX_CODER_MIN_NUM;
  }
#endif
  return decodeEnd;

#else

  if ((void*)decodep != inputp) memcpy(decodep, inputp, (size_t)widthTimesChannels * sizeof(float));

  return decodep + widthTimesChannels;

#endif
}

static void TGFX_CODER_NAME(EncodeFloatLinear)(void* outputp, int widthTimesChannels,
                                               float const* encode) {
#if !defined(TGFX_FLOAT_HIGH_CLAMP) && !defined(TGFX_FLOAT_LO_CLAMP) && \
    !defined(TGFX_DECODE_SWIZZLE)

  if ((void*)outputp != (void*)encode)
    memcpy(outputp, encode, (size_t)widthTimesChannels * sizeof(float));

#else

  float TGFX_SIMD_STREAMOUT_PTR(*) output = (float*)outputp;
  float* endOutput = ((float*)output) + widthTimesChannels;

#ifdef TGFX_FLOAT_HIGH_CLAMP
#define TGFX_SCALAR_HI_CLAMP(v) \
  if (v > TGFX_FLOAT_HIGH_CLAMP) v = TGFX_FLOAT_HIGH_CLAMP;
#else
#define TGFX_SCALAR_HI_CLAMP(v)
#endif
#ifdef TGFX_FLOAT_LOW_CLAMP
#define TGFX_SCALAR_LO_CLAMP(v) \
  if (v < TGFX_FLOAT_LOW_CLAMP) v = TGFX_FLOAT_LOW_CLAMP;
#else
#define TGFX_SCALAR_LO_CLAMP(v)
#endif

// try to do blocks of 4 when you can
#if TGFX_CODER_MIN_NUM != 3  // doesn't divide cleanly by four
  output += 4;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  while (output <= endOutput) {
    float e;
    TGFX_SIMD_NO_UNROLL(encode);
    e = encode[TGFX_ENCODE_ORDER0];
    TGFX_SCALAR_HI_CLAMP(e);
    TGFX_SCALAR_LO_CLAMP(e);
    output[0 - 4] = e;
    e = encode[TGFX_ENCODE_ORDER1];
    TGFX_SCALAR_HI_CLAMP(e);
    TGFX_SCALAR_LO_CLAMP(e);
    output[1 - 4] = e;
    e = encode[TGFX_ENCODE_ORDER2];
    TGFX_SCALAR_HI_CLAMP(e);
    TGFX_SCALAR_LO_CLAMP(e);
    output[2 - 4] = e;
    e = encode[TGFX_ENCODE_ORDER3];
    TGFX_SCALAR_HI_CLAMP(e);
    TGFX_SCALAR_LO_CLAMP(e);
    output[3 - 4] = e;
    output += 4;
    encode += 4;
  }
  output -= 4;

#endif

// do the remnants
#if TGFX_CODER_MIN_NUM < 4
  TGFX_NO_UNROLL_LOOP_START
  while (output < endOutput) {
    float e;
    TGFX_NO_UNROLL(encode);
    e = encode[TGFX_ENCODE_ORDER0];
    TGFX_SCALAR_HI_CLAMP(e);
    TGFX_SCALAR_LO_CLAMP(e);
    output[0] = e;
#if TGFX_CODER_MIN_NUM >= 2
    e = encode[TGFX_ENCODE_ORDER1];
    TGFX_SCALAR_HI_CLAMP(e);
    TGFX_SCALAR_LO_CLAMP(e);
    output[1] = e;
#endif
#if TGFX_CODER_MIN_NUM >= 3
    e = encode[TGFX_ENCODE_ORDER2];
    TGFX_SCALAR_HI_CLAMP(e);
    TGFX_SCALAR_LO_CLAMP(e);
    output[2] = e;
#endif
    output += TGFX_CODER_MIN_NUM;
    encode += TGFX_CODER_MIN_NUM;
  }
#endif

#endif
}

#undef TGFX_DECODE_SUFFIX
#undef TGFX_DECODE_ORDER0
#undef TGFX_DECODE_ORDER1
#undef TGFX_DECODE_ORDER2
#undef TGFX_DECODE_ORDER3
#undef TGFX_ENCODE_ORDER0
#undef TGFX_ENCODE_ORDER1
#undef TGFX_ENCODE_ORDER2
#undef TGFX_ENCODE_ORDER3
#undef TGFX_CODER_NAME
#undef TGFX_CODER_MIN_NUM
#undef TGFX_DECODE_SWIZZLE
#undef TGFX_SCALAR_HI_CLAMP
#undef TGFX_SCALAR_LO_CLAMP
#undef TGFX_IMAGE_RESIZE_DO_CODERS

#elif defined(TGFX_IMAGE_RESIZE_DO_VERTICALS)

#ifdef TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#define TGFX_CHANS(start, end) TGFX_STRS_JOIN14(start, TGFX_VERTICAL_CHANNELS, end, Cont)
#else
#define TGFX_CHANS(start, end) TGFX_STR_JOIN1(start, TGFX_VERTICAL_CHANNELS, end)
#endif

#if TGFX_VERTICAL_CHANNELS >= 1
#define TGFXIF0(code) code
#else
#define TGFXIF0(code)
#endif
#if TGFX_VERTICAL_CHANNELS >= 2
#define TGFXIF1(code) code
#else
#define TGFXIF1(code)
#endif
#if TGFX_VERTICAL_CHANNELS >= 3
#define TGFXIF2(code) code
#else
#define TGFXIF2(code)
#endif
#if TGFX_VERTICAL_CHANNELS >= 4
#define TGFXIF3(code) code
#else
#define TGFXIF3(code)
#endif
#if TGFX_VERTICAL_CHANNELS >= 5
#define TGFXIF4(code) code
#else
#define TGFXIF4(code)
#endif
#if TGFX_VERTICAL_CHANNELS >= 6
#define TGFXIF5(code) code
#else
#define TGFXIF5(code)
#endif
#if TGFX_VERTICAL_CHANNELS >= 7
#define TGFXIF6(code) code
#else
#define TGFXIF6(code)
#endif
#if TGFX_VERTICAL_CHANNELS >= 8
#define TGFXIF7(code) code
#else
#define TGFXIF7(code)
#endif

static void TGFX_CHANS(VerticalScatterWith, Coeffs)(float** outputs,
                                                    float const* verticalCoefficients,
                                                    float const* input, float const* inputEnd) {
  TGFXIF0(float TGFX_SIMD_STREAMOUT_PTR(*) output0 = outputs[0];
          float c0s = verticalCoefficients[0];)
  TGFXIF1(float TGFX_SIMD_STREAMOUT_PTR(*) output1 = outputs[1];
          float c1s = verticalCoefficients[1];)
  TGFXIF2(float TGFX_SIMD_STREAMOUT_PTR(*) output2 = outputs[2];
          float c2s = verticalCoefficients[2];)
  TGFXIF3(float TGFX_SIMD_STREAMOUT_PTR(*) output3 = outputs[3];
          float c3s = verticalCoefficients[3];)
  TGFXIF4(float TGFX_SIMD_STREAMOUT_PTR(*) output4 = outputs[4];
          float c4s = verticalCoefficients[4];)
  TGFXIF5(float TGFX_SIMD_STREAMOUT_PTR(*) output5 = outputs[5];
          float c5s = verticalCoefficients[5];)
  TGFXIF6(float TGFX_SIMD_STREAMOUT_PTR(*) output6 = outputs[6];
          float c6s = verticalCoefficients[6];)
  TGFXIF7(float TGFX_SIMD_STREAMOUT_PTR(*) output7 = outputs[7];
          float c7s = verticalCoefficients[7];)

  TGFX_NO_UNROLL_LOOP_START
  while (((char*)inputEnd - (char*)input) >= 16) {
    float r0, r1, r2, r3;
    TGFX_NO_UNROLL(input);

    r0 = input[0], r1 = input[1], r2 = input[2], r3 = input[3];

#ifdef TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
    TGFXIF0(output0[0] += (r0 * c0s); output0[1] += (r1 * c0s); output0[2] += (r2 * c0s);
            output0[3] += (r3 * c0s);)
    TGFXIF1(output1[0] += (r0 * c1s); output1[1] += (r1 * c1s); output1[2] += (r2 * c1s);
            output1[3] += (r3 * c1s);)
    TGFXIF2(output2[0] += (r0 * c2s); output2[1] += (r1 * c2s); output2[2] += (r2 * c2s);
            output2[3] += (r3 * c2s);)
    TGFXIF3(output3[0] += (r0 * c3s); output3[1] += (r1 * c3s); output3[2] += (r2 * c3s);
            output3[3] += (r3 * c3s);)
    TGFXIF4(output4[0] += (r0 * c4s); output4[1] += (r1 * c4s); output4[2] += (r2 * c4s);
            output4[3] += (r3 * c4s);)
    TGFXIF5(output5[0] += (r0 * c5s); output5[1] += (r1 * c5s); output5[2] += (r2 * c5s);
            output5[3] += (r3 * c5s);)
    TGFXIF6(output6[0] += (r0 * c6s); output6[1] += (r1 * c6s); output6[2] += (r2 * c6s);
            output6[3] += (r3 * c6s);)
    TGFXIF7(output7[0] += (r0 * c7s); output7[1] += (r1 * c7s); output7[2] += (r2 * c7s);
            output7[3] += (r3 * c7s);)
#else
    TGFXIF0(output0[0] = (r0 * c0s); output0[1] = (r1 * c0s); output0[2] = (r2 * c0s);
            output0[3] = (r3 * c0s);)
    TGFXIF1(output1[0] = (r0 * c1s); output1[1] = (r1 * c1s); output1[2] = (r2 * c1s);
            output1[3] = (r3 * c1s);)
    TGFXIF2(output2[0] = (r0 * c2s); output2[1] = (r1 * c2s); output2[2] = (r2 * c2s);
            output2[3] = (r3 * c2s);)
    TGFXIF3(output3[0] = (r0 * c3s); output3[1] = (r1 * c3s); output3[2] = (r2 * c3s);
            output3[3] = (r3 * c3s);)
    TGFXIF4(output4[0] = (r0 * c4s); output4[1] = (r1 * c4s); output4[2] = (r2 * c4s);
            output4[3] = (r3 * c4s);)
    TGFXIF5(output5[0] = (r0 * c5s); output5[1] = (r1 * c5s); output5[2] = (r2 * c5s);
            output5[3] = (r3 * c5s);)
    TGFXIF6(output6[0] = (r0 * c6s); output6[1] = (r1 * c6s); output6[2] = (r2 * c6s);
            output6[3] = (r3 * c6s);)
    TGFXIF7(output7[0] = (r0 * c7s); output7[1] = (r1 * c7s); output7[2] = (r2 * c7s);
            output7[3] = (r3 * c7s);)
#endif

    input += 4;
    TGFXIF0(output0 += 4;)
    TGFXIF1(output1 += 4;)
    TGFXIF2(output2 += 4;)
    TGFXIF3(output3 += 4;)
    TGFXIF4(output4 += 4;)
    TGFXIF5(output5 += 4;)
    TGFXIF6(output6 += 4;)
    TGFXIF7(output7 += 4;)
  }
  TGFX_NO_UNROLL_LOOP_START
  while (input < inputEnd) {
    float r = input[0];
    TGFX_NO_UNROLL(output0);

#ifdef TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
    TGFXIF0(output0[0] += (r * c0s);)
    TGFXIF1(output1[0] += (r * c1s);)
    TGFXIF2(output2[0] += (r * c2s);)
    TGFXIF3(output3[0] += (r * c3s);)
    TGFXIF4(output4[0] += (r * c4s);)
    TGFXIF5(output5[0] += (r * c5s);)
    TGFXIF6(output6[0] += (r * c6s);)
    TGFXIF7(output7[0] += (r * c7s);)
#else
    TGFXIF0(output0[0] = (r * c0s);)
    TGFXIF1(output1[0] = (r * c1s);)
    TGFXIF2(output2[0] = (r * c2s);)
    TGFXIF3(output3[0] = (r * c3s);)
    TGFXIF4(output4[0] = (r * c4s);)
    TGFXIF5(output5[0] = (r * c5s);)
    TGFXIF6(output6[0] = (r * c6s);)
    TGFXIF7(output7[0] = (r * c7s);)
#endif

    ++input;
    TGFXIF0(++output0;)
    TGFXIF1(++output1;)
    TGFXIF2(++output2;)
    TGFXIF3(++output3;)
    TGFXIF4(++output4;)
    TGFXIF5(++output5;)
    TGFXIF6(++output6;)
    TGFXIF7(++output7;)
  }
}

static void TGFX_CHANS(VerticalGatherWith, Coeffs)(float* outputp,
                                                   float const* verticalCoefficients,
                                                   float const** inputs, float const* input0End) {
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputp;

  TGFXIF0(float const* input0 = inputs[0]; float c0s = verticalCoefficients[0];)
  TGFXIF1(float const* input1 = inputs[1]; float c1s = verticalCoefficients[1];)
  TGFXIF2(float const* input2 = inputs[2]; float c2s = verticalCoefficients[2];)
  TGFXIF3(float const* input3 = inputs[3]; float c3s = verticalCoefficients[3];)
  TGFXIF4(float const* input4 = inputs[4]; float c4s = verticalCoefficients[4];)
  TGFXIF5(float const* input5 = inputs[5]; float c5s = verticalCoefficients[5];)
  TGFXIF6(float const* input6 = inputs[6]; float c6s = verticalCoefficients[6];)
  TGFXIF7(float const* input7 = inputs[7]; float c7s = verticalCoefficients[7];)

#if (TGFX_VERTICAL_CHANNELS == 1) && !defined(TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE)
  // check single channel one weight
  if ((c0s >= (1.0f - 0.000001f)) && (c0s <= (1.0f + 0.000001f))) {
    memcpy(output, input0, (size_t)((char*)input0End - (char*)input0));
    return;
  }
#endif

  TGFX_NO_UNROLL_LOOP_START
  while (((char*)input0End - (char*)input0) >= 16) {
    float o0, o1, o2, o3;
    TGFX_NO_UNROLL(output);
#ifdef TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
    TGFXIF0(o0 = output[0] + input0[0] * c0s; o1 = output[1] + input0[1] * c0s;
            o2 = output[2] + input0[2] * c0s; o3 = output[3] + input0[3] * c0s;)
#else
    TGFXIF0(o0 = input0[0] * c0s; o1 = input0[1] * c0s; o2 = input0[2] * c0s; o3 = input0[3] * c0s;)
#endif
    TGFXIF1(o0 += input1[0] * c1s; o1 += input1[1] * c1s; o2 += input1[2] * c1s;
            o3 += input1[3] * c1s;)
    TGFXIF2(o0 += input2[0] * c2s; o1 += input2[1] * c2s; o2 += input2[2] * c2s;
            o3 += input2[3] * c2s;)
    TGFXIF3(o0 += input3[0] * c3s; o1 += input3[1] * c3s; o2 += input3[2] * c3s;
            o3 += input3[3] * c3s;)
    TGFXIF4(o0 += input4[0] * c4s; o1 += input4[1] * c4s; o2 += input4[2] * c4s;
            o3 += input4[3] * c4s;)
    TGFXIF5(o0 += input5[0] * c5s; o1 += input5[1] * c5s; o2 += input5[2] * c5s;
            o3 += input5[3] * c5s;)
    TGFXIF6(o0 += input6[0] * c6s; o1 += input6[1] * c6s; o2 += input6[2] * c6s;
            o3 += input6[3] * c6s;)
    TGFXIF7(o0 += input7[0] * c7s; o1 += input7[1] * c7s; o2 += input7[2] * c7s;
            o3 += input7[3] * c7s;)
    output[0] = o0;
    output[1] = o1;
    output[2] = o2;
    output[3] = o3;
    output += 4;
    TGFXIF0(input0 += 4;)
    TGFXIF1(input1 += 4;)
    TGFXIF2(input2 += 4;)
    TGFXIF3(input3 += 4;)
    TGFXIF4(input4 += 4;)
    TGFXIF5(input5 += 4;)
    TGFXIF6(input6 += 4;)
    TGFXIF7(input7 += 4;)
  }
  TGFX_NO_UNROLL_LOOP_START
  while (input0 < input0End) {
    float o0;
    TGFX_NO_UNROLL(output);
#ifdef TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
    TGFXIF0(o0 = output[0] + input0[0] * c0s;)
#else
    TGFXIF0(o0 = input0[0] * c0s;)
#endif
    TGFXIF1(o0 += input1[0] * c1s;)
    TGFXIF2(o0 += input2[0] * c2s;)
    TGFXIF3(o0 += input3[0] * c3s;)
    TGFXIF4(o0 += input4[0] * c4s;)
    TGFXIF5(o0 += input5[0] * c5s;)
    TGFXIF6(o0 += input6[0] * c6s;)
    TGFXIF7(o0 += input7[0] * c7s;)
    output[0] = o0;
    ++output;
    TGFXIF0(++input0;)
    TGFXIF1(++input1;)
    TGFXIF2(++input2;)
    TGFXIF3(++input3;)
    TGFXIF4(++input4;)
    TGFXIF5(++input5;)
    TGFXIF6(++input6;)
    TGFXIF7(++input7;)
  }
}

#undef TGFXIF0
#undef TGFXIF1
#undef TGFXIF2
#undef TGFXIF3
#undef TGFXIF4
#undef TGFXIF5
#undef TGFXIF6
#undef TGFXIF7
#undef TGFX_IMAGE_RESIZE_DO_VERTICALS
#undef TGFX_VERTICAL_CHANNELS
#undef TGFX_IMAGE_RESIZE_DO_HORIZONTALS
#undef TGFX_STRS_JOIN24
#undef TGFX_STRS_JOIN14
#undef TGFX_CHANS
#ifdef TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#undef TGFX_IMAGE_RESIZE_VERTICAL_CONTINUE
#endif

#else

#define TGFX_CHANS(start, end) TGFX_STR_JOIN1(start, TGFX_HORIZONTAL_CHANNELS, end)

#ifndef TGFX_2_COEFF_ONLY
#define TGFX_2_COEFF_ONLY() \
  TGFX_1_COEFF_ONLY();      \
  TGFX_1_COEFF_REMNANT(1);
#endif

#ifndef TGFX_2_COEFF_REMNANT
#define TGFX_2_COEFF_REMNANT(ofs) \
  TGFX_1_COEFF_REMNANT(ofs);      \
  TGFX_1_COEFF_REMNANT((ofs) + 1);
#endif

#ifndef TGFX_3_COEFF_ONLY
#define TGFX_3_COEFF_ONLY() \
  TGFX_2_COEFF_ONLY();      \
  TGFX_1_COEFF_REMNANT(2);
#endif

#ifndef TGFX_3_COEFF_REMNANT
#define TGFX_3_COEFF_REMNANT(ofs) \
  TGFX_2_COEFF_REMNANT(ofs);      \
  TGFX_1_COEFF_REMNANT((ofs) + 2);
#endif

#ifndef TGFX_3_COEFF_SETUP
#define TGFX_3_COEFF_SETUP()
#endif

#ifndef TGFX_4_COEFF_START
#define TGFX_4_COEFF_START() \
  TGFX_2_COEFF_ONLY();       \
  TGFX_2_COEFF_REMNANT(2);
#endif

#ifndef TGFX_4_COEFF_CONTINUE_FROM_4
#define TGFX_4_COEFF_CONTINUE_FROM_4(ofs) \
  TGFX_2_COEFF_REMNANT(ofs);              \
  TGFX_2_COEFF_REMNANT((ofs) + 2);
#endif

#ifndef TGFX_STORE_OUTPUT_TINY
#define TGFX_STORE_OUTPUT_TINY TGFX_STORE_OUTPUT
#endif

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith1Coeff)(float* outputBuffer, unsigned int outputSubSize,
                                           float const* decodeBuffer,
                                           Contributors const* horizontalContributors,
                                           float const* horizontalCoefficients,
                                           int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_1_COEFF_ONLY();
    TGFX_STORE_OUTPUT_TINY();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith2Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                            float const* decodeBuffer,
                                            Contributors const* horizontalContributors,
                                            float const* horizontalCoefficients,
                                            int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_2_COEFF_ONLY();
    TGFX_STORE_OUTPUT_TINY();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith3Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                            float const* decodeBuffer,
                                            Contributors const* horizontalContributors,
                                            float const* horizontalCoefficients,
                                            int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_3_COEFF_ONLY();
    TGFX_STORE_OUTPUT_TINY();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith4Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                            float const* decodeBuffer,
                                            Contributors const* horizontalContributors,
                                            float const* horizontalCoefficients,
                                            int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_4_COEFF_START();
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith5Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                            float const* decodeBuffer,
                                            Contributors const* horizontalContributors,
                                            float const* horizontalCoefficients,
                                            int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_4_COEFF_START();
    TGFX_1_COEFF_REMNANT(4);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith6Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                            float const* decodeBuffer,
                                            Contributors const* horizontalContributors,
                                            float const* horizontalCoefficients,
                                            int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_4_COEFF_START();
    TGFX_2_COEFF_REMNANT(4);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith7Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                            float const* decodeBuffer,
                                            Contributors const* horizontalContributors,
                                            float const* horizontalCoefficients,
                                            int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_3_COEFF_SETUP();
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;

    TGFX_4_COEFF_START();
    TGFX_3_COEFF_REMNANT(4);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith8Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                            float const* decodeBuffer,
                                            Contributors const* horizontalContributors,
                                            float const* horizontalCoefficients,
                                            int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_4_COEFF_START();
    TGFX_4_COEFF_CONTINUE_FROM_4(4);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith9Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                            float const* decodeBuffer,
                                            Contributors const* horizontalContributors,
                                            float const* horizontalCoefficients,
                                            int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_4_COEFF_START();
    TGFX_4_COEFF_CONTINUE_FROM_4(4);
    TGFX_1_COEFF_REMNANT(8);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith10Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                             float const* decodeBuffer,
                                             Contributors const* horizontalContributors,
                                             float const* horizontalCoefficients,
                                             int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_4_COEFF_START();
    TGFX_4_COEFF_CONTINUE_FROM_4(4);
    TGFX_2_COEFF_REMNANT(8);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith11Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                             float const* decodeBuffer,
                                             Contributors const* horizontalContributors,
                                             float const* horizontalCoefficients,
                                             int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_3_COEFF_SETUP();
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_4_COEFF_START();
    TGFX_4_COEFF_CONTINUE_FROM_4(4);
    TGFX_3_COEFF_REMNANT(8);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWith12Coeffs)(float* outputBuffer, unsigned int outputSubSize,
                                             float const* decodeBuffer,
                                             Contributors const* horizontalContributors,
                                             float const* horizontalCoefficients,
                                             int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    float const* hc = horizontalCoefficients;
    TGFX_4_COEFF_START();
    TGFX_4_COEFF_CONTINUE_FROM_4(4);
    TGFX_4_COEFF_CONTINUE_FROM_4(8);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWithNCoeffsMod0)(float* outputBuffer, unsigned int outputSubSize,
                                                float const* decodeBuffer,
                                                Contributors const* horizontalContributors,
                                                float const* horizontalCoefficients,
                                                int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    int n = ((horizontalContributors->n1 - horizontalContributors->n0 + 1) - 4 + 3) >> 2;
    float const* hc = horizontalCoefficients;

    TGFX_4_COEFF_START();
    TGFX_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += TGFX_HORIZONTAL_CHANNELS * 4;
      TGFX_4_COEFF_CONTINUE_FROM_4(0);
      --n;
    } while (n > 0);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWithNCoeffsMod1)(float* outputBuffer, unsigned int outputSubSize,
                                                float const* decodeBuffer,
                                                Contributors const* horizontalContributors,
                                                float const* horizontalCoefficients,
                                                int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    int n = ((horizontalContributors->n1 - horizontalContributors->n0 + 1) - 5 + 3) >> 2;
    float const* hc = horizontalCoefficients;

    TGFX_4_COEFF_START();
    TGFX_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += TGFX_HORIZONTAL_CHANNELS * 4;
      TGFX_4_COEFF_CONTINUE_FROM_4(0);
      --n;
    } while (n > 0);
    TGFX_1_COEFF_REMNANT(4);
    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWithNCoeffsMod2)(float* outputBuffer, unsigned int outputSubSize,
                                                float const* decodeBuffer,
                                                Contributors const* horizontalContributors,
                                                float const* horizontalCoefficients,
                                                int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    int n = ((horizontalContributors->n1 - horizontalContributors->n0 + 1) - 6 + 3) >> 2;
    float const* hc = horizontalCoefficients;

    TGFX_4_COEFF_START();
    TGFX_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += TGFX_HORIZONTAL_CHANNELS * 4;
      TGFX_4_COEFF_CONTINUE_FROM_4(0);
      --n;
    } while (n > 0);
    TGFX_2_COEFF_REMNANT(4);

    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static void TGFX_CHANS(HorizontalGather,
                       ChannelsWithNCoeffsMod3)(float* outputBuffer, unsigned int outputSubSize,
                                                float const* decodeBuffer,
                                                Contributors const* horizontalContributors,
                                                float const* horizontalCoefficients,
                                                int coefficientWidth) {
  float const* outputEnd = outputBuffer + outputSubSize * TGFX_HORIZONTAL_CHANNELS;
  float TGFX_SIMD_STREAMOUT_PTR(*) output = outputBuffer;
  TGFX_3_COEFF_SETUP();
  TGFX_SIMD_NO_UNROLL_LOOP_START
  do {
    float const* decode = decodeBuffer + horizontalContributors->n0 * TGFX_HORIZONTAL_CHANNELS;
    int n = ((horizontalContributors->n1 - horizontalContributors->n0 + 1) - 7 + 3) >> 2;
    float const* hc = horizontalCoefficients;

    TGFX_4_COEFF_START();
    TGFX_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += TGFX_HORIZONTAL_CHANNELS * 4;
      TGFX_4_COEFF_CONTINUE_FROM_4(0);
      --n;
    } while (n > 0);
    TGFX_3_COEFF_REMNANT(4);

    TGFX_STORE_OUTPUT();
  } while (output < outputEnd);
}

static HorizontalGatherChannelsFunc* TGFX_CHANS(HorizontalGather, ChannelsWithNCoeffsFuncs)[4] = {
    TGFX_CHANS(HorizontalGather, ChannelsWithNCoeffsMod0),
    TGFX_CHANS(HorizontalGather, ChannelsWithNCoeffsMod1),
    TGFX_CHANS(HorizontalGather, ChannelsWithNCoeffsMod2),
    TGFX_CHANS(HorizontalGather, ChannelsWithNCoeffsMod3),
};

static HorizontalGatherChannelsFunc* TGFX_CHANS(HorizontalGather, ChannelsFuncs)[12] = {
    TGFX_CHANS(HorizontalGather, ChannelsWith1Coeff),
    TGFX_CHANS(HorizontalGather, ChannelsWith2Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith3Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith4Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith5Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith6Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith7Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith8Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith9Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith10Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith11Coeffs),
    TGFX_CHANS(HorizontalGather, ChannelsWith12Coeffs),
};

#undef TGFX_HORIZONTAL_CHANNELS
#undef TGFX_IMAGE_RESIZE_DO_HORIZONTALS
#undef TGFX_1_COEFF_ONLY
#undef TGFX_1_COEFF_REMNANT
#undef TGFX_2_COEFF_ONLY
#undef TGFX_2_COEFF_REMNANT
#undef TGFX_3_COEFF_ONLY
#undef TGFX_3_COEFF_REMNANT
#undef TGFX_3_COEFF_SETUP
#undef TGFX_4_COEFF_START
#undef TGFX_4_COEFF_CONTINUE_FROM_4
#undef TGFX_STORE_OUTPUT
#undef TGFX_STORE_OUTPUT_TINY
#undef TGFX_CHANS

#endif

#endif