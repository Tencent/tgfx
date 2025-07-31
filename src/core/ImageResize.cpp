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
#include <tgfx/core/Buffer.h>
#include <tgfx/core/Pixmap.h>
#if !defined(STB_IMAGE_RESIZE_DO_HORIZONTALS) && !defined(STB_IMAGE_RESIZE_DO_VERTICALS) && !defined(STB_IMAGE_RESIZE_DO_CODERS)
#include "ImageResize.h"
#include <assert.h>
#include <cstring>
#include <math.h>

namespace tgfx {

#ifndef SOURCEFILE
#define SOURCEFILE "ImageResize.cpp"
#endif

#define stbir__max_uint8_as_float             255.0f
#define stbir__max_uint16_as_float            65535.0f
#define stbir__max_uint8_as_float_inverted    3.9215689e-03f     // (1.0f/255.0f)
#define stbir__max_uint16_as_float_inverted   1.5259022e-05f     // (1.0f/65535.0f)
#define stbir__small_float ((float)1 / (1 << 20) / (1 << 20) / (1 << 20) / (1 << 20) / (1 << 20) / (1 << 20))

#ifdef _MSC_VER

#define stbir__inline __forceinline

#else

#define stbir__inline __inline__

#endif

// min/max friendly
#define STBIR_CLAMP(x, xmin, xmax) for(;;) { \
if ( (x) < (xmin) ) (x) = (xmin);     \
if ( (x) > (xmax) ) (x) = (xmax);     \
break;                                \
}

static stbir__inline int stbir__min(int a, int b)
{
  return a < b ? a : b;
}

static stbir__inline int stbir__max(int a, int b)
{
  return a > b ? a : b;
}

static float stbir__srgb_uchar_to_linear_float[256] = {
  0.000000f, 0.000304f, 0.000607f, 0.000911f, 0.001214f, 0.001518f, 0.001821f, 0.002125f, 0.002428f, 0.002732f, 0.003035f,
  0.003347f, 0.003677f, 0.004025f, 0.004391f, 0.004777f, 0.005182f, 0.005605f, 0.006049f, 0.006512f, 0.006995f, 0.007499f,
  0.008023f, 0.008568f, 0.009134f, 0.009721f, 0.010330f, 0.010960f, 0.011612f, 0.012286f, 0.012983f, 0.013702f, 0.014444f,
  0.015209f, 0.015996f, 0.016807f, 0.017642f, 0.018500f, 0.019382f, 0.020289f, 0.021219f, 0.022174f, 0.023153f, 0.024158f,
  0.025187f, 0.026241f, 0.027321f, 0.028426f, 0.029557f, 0.030713f, 0.031896f, 0.033105f, 0.034340f, 0.035601f, 0.036889f,
  0.038204f, 0.039546f, 0.040915f, 0.042311f, 0.043735f, 0.045186f, 0.046665f, 0.048172f, 0.049707f, 0.051269f, 0.052861f,
  0.054480f, 0.056128f, 0.057805f, 0.059511f, 0.061246f, 0.063010f, 0.064803f, 0.066626f, 0.068478f, 0.070360f, 0.072272f,
  0.074214f, 0.076185f, 0.078187f, 0.080220f, 0.082283f, 0.084376f, 0.086500f, 0.088656f, 0.090842f, 0.093059f, 0.095307f,
  0.097587f, 0.099899f, 0.102242f, 0.104616f, 0.107023f, 0.109462f, 0.111932f, 0.114435f, 0.116971f, 0.119538f, 0.122139f,
  0.124772f, 0.127438f, 0.130136f, 0.132868f, 0.135633f, 0.138432f, 0.141263f, 0.144128f, 0.147027f, 0.149960f, 0.152926f,
  0.155926f, 0.158961f, 0.162029f, 0.165132f, 0.168269f, 0.171441f, 0.174647f, 0.177888f, 0.181164f, 0.184475f, 0.187821f,
  0.191202f, 0.194618f, 0.198069f, 0.201556f, 0.205079f, 0.208637f, 0.212231f, 0.215861f, 0.219526f, 0.223228f, 0.226966f,
  0.230740f, 0.234551f, 0.238398f, 0.242281f, 0.246201f, 0.250158f, 0.254152f, 0.258183f, 0.262251f, 0.266356f, 0.270498f,
  0.274677f, 0.278894f, 0.283149f, 0.287441f, 0.291771f, 0.296138f, 0.300544f, 0.304987f, 0.309469f, 0.313989f, 0.318547f,
  0.323143f, 0.327778f, 0.332452f, 0.337164f, 0.341914f, 0.346704f, 0.351533f, 0.356400f, 0.361307f, 0.366253f, 0.371238f,
  0.376262f, 0.381326f, 0.386430f, 0.391573f, 0.396755f, 0.401978f, 0.407240f, 0.412543f, 0.417885f, 0.423268f, 0.428691f,
  0.434154f, 0.439657f, 0.445201f, 0.450786f, 0.456411f, 0.462077f, 0.467784f, 0.473532f, 0.479320f, 0.485150f, 0.491021f,
  0.496933f, 0.502887f, 0.508881f, 0.514918f, 0.520996f, 0.527115f, 0.533276f, 0.539480f, 0.545725f, 0.552011f, 0.558340f,
  0.564712f, 0.571125f, 0.577581f, 0.584078f, 0.590619f, 0.597202f, 0.603827f, 0.610496f, 0.617207f, 0.623960f, 0.630757f,
  0.637597f, 0.644480f, 0.651406f, 0.658375f, 0.665387f, 0.672443f, 0.679543f, 0.686685f, 0.693872f, 0.701102f, 0.708376f,
  0.715694f, 0.723055f, 0.730461f, 0.737911f, 0.745404f, 0.752942f, 0.760525f, 0.768151f, 0.775822f, 0.783538f, 0.791298f,
  0.799103f, 0.806952f, 0.814847f, 0.822786f, 0.830770f, 0.838799f, 0.846873f, 0.854993f, 0.863157f, 0.871367f, 0.879622f,
  0.887923f, 0.896269f, 0.904661f, 0.913099f, 0.921582f, 0.930111f, 0.938686f, 0.947307f, 0.955974f, 0.964686f, 0.973445f,
  0.982251f, 0.991102f, 1.0f
};

typedef union
{
  unsigned int u;
  float f;
} stbir__FP32;

static const uint32_t fp32_to_srgb8_tab4[104] = {
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

static stbir__inline uint8_t stbir__linear_to_srgb_uchar(float in)
{
  static const stbir__FP32 almostone = { 0x3f7fffff }; // 1-eps
  static const stbir__FP32 minval = { (127-13) << 23 };
  uint32_t tab,bias,scale,t;
  stbir__FP32 f;

  // Clamp to [2^(-13), 1-eps]; these two values map to 0 and 1, respectively.
  // The tests are carefully written so that NaNs map to 0, same as in the reference
  // implementation.
  if (!(in > minval.f)) // written this way to catch NaNs
    return 0;
  if (in > almostone.f)
    return 255;

  // Do the table lookup and unpack bias, scale
  f.f = in;
  tab = fp32_to_srgb8_tab4[(f.u - minval.u) >> 20];
  bias = (tab >> 16) << 9;
  scale = tab & 0xffff;

  // Grab next-highest mantissa bits and perform linear interpolation
  t = (f.u >> 12) & 0xff;
  return (unsigned char) ((bias + scale*t) >> 16);
}

typedef union stbir__FP16
{
  unsigned short u;
} stbir__FP16;

static stbir__inline float stbir__half_to_float( stbir__FP16 h )
{
  static const stbir__FP32 magic = { (254 - 15) << 23 };
  static const stbir__FP32 was_infnan = { (127 + 16) << 23 };
  stbir__FP32 o;

  o.u = (unsigned int)(h.u & 0x7fff) << 13;     // exponent/mantissa bits
  o.f *= magic.f;                 // exponent adjust
  if (o.f >= was_infnan.f)        // make sure Inf/NaN survive
    o.u |= 255 << 23;
  o.u |= (unsigned int)(h.u & 0x8000) << 16;    // sign bit
  return o.f;
}

static stbir__inline stbir__FP16 stbir__float_to_half(float val)
{
  stbir__FP32 f32infty = { 255 << 23 };
  stbir__FP32 f16max   = { (127 + 16) << 23 };
  stbir__FP32 denorm_magic = { ((127 - 15) + (23 - 10) + 1) << 23 };
  unsigned int sign_mask = 0x80000000u;
  stbir__FP16 o = { 0 };
  stbir__FP32 f;
  unsigned int sign;

  f.f = val;
  sign = f.u & sign_mask;
  f.u ^= sign;

  if (f.u >= f16max.u) // result is Inf or NaN (all exponent bits set)
    o.u = (f.u > f32infty.u) ? 0x7e00 : 0x7c00; // NaN->qNaN and Inf->Inf
  else // (De)normalized number or zero
  {
    if (f.u < (113 << 23)) // resulting FP16 is subnormal or zero
    {
      // use a magic value to align our 10 mantissa bits at the bottom of
      // the float. as long as FP addition is round-to-nearest-even this
      // just works.
      f.f += denorm_magic.f;
      // and one integer subtract of the bias later, we have our final float!
      o.u = (unsigned short) ( f.u - denorm_magic.u );
    }
    else
    {
      unsigned int mant_odd = (f.u >> 13) & 1; // resulting mantissa is odd
      // update exponent, rounding bias part 1
      f.u = f.u + ((15u - 127) << 23) + 0xfff;
      // rounding bias part 2
      f.u += mant_odd;
      // take the bits!
      o.u = (unsigned short) ( f.u >> 13 );
    }
  }

  o.u |= sign >> 16;
  return o;
}

typedef struct
{
  int n0; // First contributing pixel
  int n1; // Last contributing pixel
} stbir__contributors;

typedef struct stbir__scale_info
{
  int input_full_size;
  int output_sub_size;
  float scale;
  float inv_scale;
  float pixel_shift; // starting shift in output pixel space (in pixels)
  int scale_is_rational;
  uint32_t scale_numerator, scale_denominator;
} stbir__scale_info;

typedef struct
{
  int lowest;    // First sample index for whole filter
  int highest;   // Last sample index for whole filter
  int widest;    // widest single set of samples for an output
} stbir__filter_extent_info;

typedef struct
{
  stbir__contributors * contributors;
  float* coefficients;
  stbir__contributors * gather_prescatter_contributors;
  float * gather_prescatter_coefficients;
  stbir__scale_info scale_info;
  float support;
  int coefficient_width;
  int filter_pixel_width;
  int filter_pixel_margin;
  int num_contributors;
  int contributors_size;
  int coefficients_size;
  stbir__filter_extent_info extent_info;
  int is_gather;  // 0 = scatter, 1 = gather with scale >= 1, 2 = gather with scale < 1
  int gather_prescatter_num_contributors;
  int gather_prescatter_coefficient_width;
  int gather_prescatter_contributors_size;
  int gather_prescatter_coefficients_size;
} stbir__sampler;

typedef struct
{
  int n0; // First pixel of decode buffer to write to
  int n1; // Last pixel of decode that will be written to
  int pixel_offset_for_input;  // Pixel offset into input_scanline
} stbir__span;

typedef struct
{
  stbir__contributors conservative;
  int edge_sizes[2];    // this can be less than filter_pixel_margin, if the filter and scaling falls off
  stbir__span spans[2]; // can be two spans, if doing input subrect with clamp mode WRAP
} stbir__extents;

typedef struct
{
  float* decode_buffer;

  int ring_buffer_first_scanline;
  int ring_buffer_last_scanline;
  int ring_buffer_begin_index;    // first_scanline is at this index in the ring buffer
  int start_output_y, end_output_y;
  int start_input_y, end_input_y;  // used in scatter only
  float* ring_buffer;  // one big buffer that we index into

  float* vertical_buffer;

  char no_cache_straddle[64];
} stbir__per_split_info;

typedef float * stbir__decode_pixels_func( float * decode, int width_times_channels, void const * input );
typedef void stbir__alpha_weight_func( float * decode_buffer, int width_times_channels );
typedef void stbir__horizontal_gather_channels_func( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer,
  stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width );
typedef void stbir__alpha_unweight_func(float * encode_buffer, int width_times_channels );
typedef void stbir__encode_pixels_func( void * output, int width_times_channels, float const * encode );

// the internal pixel layout enums are in a different order, so we can easily do range comparisons of types
//   the public pixel layout is ordered in a way that if you cast num_channels (1-4) to the enum, you get something sensible
typedef enum
{
  STBIRI_1CHANNEL = 0,
  STBIRI_2CHANNEL = 1,
  STBIRI_RGB      = 2,
  STBIRI_BGR      = 3,
  STBIRI_4CHANNEL = 4,

  STBIRI_RGBA = 5,
  STBIRI_BGRA = 6,
  STBIRI_ARGB = 7,
  STBIRI_ABGR = 8,
  STBIRI_RA   = 9,
  STBIRI_AR   = 10,

  STBIRI_RGBA_PM = 11,
  STBIRI_BGRA_PM = 12,
  STBIRI_ARGB_PM = 13,
  STBIRI_ABGR_PM = 14,
  STBIRI_RA_PM   = 15,
  STBIRI_AR_PM   = 16,
} stbir_internal_pixel_layout;

struct ResizeInfo
{
  stbir__sampler horizontal;
  stbir__sampler vertical;

  void const * input_data;
  void * output_data;

  int input_stride_bytes;
  int output_stride_bytes;
  int ring_buffer_length_bytes;   // The length of an individual entry in the ring buffer. The total number of ring buffers is stbir__get_filter_pixel_width(filter)
  int ring_buffer_num_entries;    // Total number of entries in the ring buffer.

  DataType input_type;
  DataType output_type;

  void * user_data;

  stbir__extents scanline_extents;

  void * alloced_mem;
  stbir__per_split_info * split_info;  // by default 1, but there will be N of these allocated based on the thread init you did

  stbir__decode_pixels_func * decode_pixels;
  stbir__alpha_weight_func * alpha_weight;
  stbir__horizontal_gather_channels_func * horizontal_gather_channels;
  stbir__alpha_unweight_func * alpha_unweight;
  stbir__encode_pixels_func * encode_pixels;

  int alloc_ring_buffer_num_entries;    // Number of entries in the ring buffer that will be allocated
  int splits; // count of splits

  stbir_internal_pixel_layout input_pixel_layout_internal;
  stbir_internal_pixel_layout output_pixel_layout_internal;

  int input_color_and_type;
  int offset_x, offset_y; // offset within output_data
  int vertical_first;
  int channels;
  int effective_channels; // same as channels, except on RGBA/ARGB (7), or XA/AX (3)
  size_t alloced_total;
};

struct ResizeData {
  void* userData;
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

// layout lookups - must match stbir_internal_pixel_layout
static unsigned char stbir__pixel_channels[] = {
  1,2,3,3,4,   // 1ch, 2ch, rgb, bgr, 4ch
  4,4,4,4,2,2, // RGBA,BGRA,ARGB,ABGR,RA,AR
  4,4,4,4,2,2, // RGBA_PM,BGRA_PM,ARGB_PM,ABGR_PM,RA_PM,AR_PM
};

static int stbir__check_output_stuff( void ** ret_ptr, int * ret_pitch, void * output_pixels, int type_size, int output_w, int output_h, int output_stride_in_bytes, stbir_internal_pixel_layout pixel_layout )
{
  size_t size;
  int pitch;
  void * ptr;

  pitch = output_w * type_size * stbir__pixel_channels[ pixel_layout ];
  if ( pitch == 0 )
    return 0;

  if ( output_stride_in_bytes == 0 )
    output_stride_in_bytes = pitch;

  if ( output_stride_in_bytes < pitch )
    return 0;

  size = (size_t)output_stride_in_bytes * (size_t)output_h;
  if ( size == 0 )
    return 0;

  *ret_ptr = 0;
  *ret_pitch = output_stride_in_bytes;

  if ( output_pixels == 0 )
  {
    ptr = malloc(size);
    if ( ptr == 0 )
      return 0;

    *ret_ptr = ptr;
    *ret_pitch = pitch;
  }

  return 1;
}

// must match stbir_datatype
static unsigned char stbir__type_size[] = {
  1,1,1,2,4,2 // STBIR_TYPE_UINT8,STBIR_TYPE_UINT8_SRGB,STBIR_TYPE_UINT8_SRGB_ALPHA,STBIR_TYPE_UINT16,STBIR_TYPE_FLOAT,STBIR_TYPE_HALF_FLOAT
};

// the internal pixel layout enums are in a different order, so we can easily do range comparisons of types
//   the public pixel layout is ordered in a way that if you cast num_channels (1-4) to the enum, you get something sensible
static stbir_internal_pixel_layout stbir__pixel_layout_convert_public_to_internal[] = {
  STBIRI_BGR, STBIRI_1CHANNEL, STBIRI_2CHANNEL, STBIRI_RGB, STBIRI_RGBA,
  STBIRI_4CHANNEL, STBIRI_BGRA, STBIRI_ARGB, STBIRI_ABGR, STBIRI_RA, STBIRI_AR,
  STBIRI_RGBA_PM, STBIRI_BGRA_PM, STBIRI_ARGB_PM, STBIRI_ABGR_PM, STBIRI_RA_PM, STBIRI_AR_PM,
};

static void stbir__init_and_set_layout( ResizeData * resize, PixelLayout pixelLayout, DataType dataType )
{
  resize->userData = resize;
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

static void stbir_resize_init( ResizeData * resize,
                                 const void *input_pixels,  int input_w,  int input_h, int input_stride_in_bytes, // stride can be zero
                                       void *output_pixels, int output_w, int output_h, int output_stride_in_bytes, // stride can be zero
                                 PixelLayout pixel_layout, DataType data_type )
{
  resize->inputPixels = input_pixels;
  resize->inputW = input_w;
  resize->intputH = input_h;
  resize->inputStrideInBytes = input_stride_in_bytes;
  resize->outputPixels = output_pixels;
  resize->outputW = output_w;
  resize->outputH = output_h;
  resize->outputStrideInBytes = output_stride_in_bytes;

  stbir__init_and_set_layout( resize, pixel_layout, data_type );
}

static void stbir__free_internal_mem( ResizeInfo *info )
{
#define STBIR__FREE_AND_CLEAR( ptr ) { if ( ptr ) { void * p = (ptr); (ptr) = 0; free(p); } }
  if ( info )
  {
    STBIR__FREE_AND_CLEAR( info->alloced_mem );
  }
#undef STBIR__FREE_AND_CLEAR
}

void stbir_free_samplers( ResizeData * resize )
{
  if ( resize->samplers )
  {
    stbir__free_internal_mem( resize->samplers );
    resize->samplers = 0;
    resize->calledAlloc = 0;
  }
}

static void stbir__clip( int * outx, int * outsubw, int outw, double * u0, double * u1 )
{
  double per, adj;
  int over;

  // do left/top edge
  if ( *outx < 0 )
  {
    per = ( (double)*outx ) / ( (double)*outsubw ); // is negative
    adj = per * ( *u1 - *u0 );
    *u0 -= adj; // increases u0
    *outx = 0;
  }

  // do right/bot edge
  over = outw - ( *outx + *outsubw );
  if ( over < 0 )
  {
    per = ( (double)over ) / ( (double)*outsubw ); // is negative
    adj = per * ( *u1 - *u0 );
    *u1 += adj; // decrease u1
    *outsubw = outw - *outx;
  }
}

// converts a double to a rational that has less than one float bit of error (returns 0 if unable to do so)
static int stbir__double_to_rational(double f, uint32_t limit, uint32_t *numer, uint32_t *denom, int limit_denom ) // limit_denom (1) or limit numer (0)
{
  double err;
  uint64_t top, bot;
  uint64_t numer_last = 0;
  uint64_t denom_last = 1;
  uint64_t numer_estimate = 1;
  uint64_t denom_estimate = 0;

  // scale to past float error range
  top = (uint64_t)( f * (double)(1 << 25) );
  bot = 1 << 25;

  // keep refining, but usually stops in a few loops - usually 5 for bad cases
  for(;;)
  {
    uint64_t est, temp;

    // hit limit, break out and do best full range estimate
    if ( ( ( limit_denom ) ? denom_estimate : numer_estimate ) >= limit )
      break;

    // is the current error less than 1 bit of a float? if so, we're done
    if ( denom_estimate )
    {
      err = ( (double)numer_estimate / (double)denom_estimate ) - f;
      if ( err < 0.0 ) err = -err;
      if ( err < ( 1.0 / (double)(1<<24) ) )
      {
        // yup, found it
        *numer = (uint32_t) numer_estimate;
        *denom = (uint32_t) denom_estimate;
        return 1;
      }
    }

    // no more refinement bits left? break out and do full range estimate
    if ( bot == 0 )
      break;

    // gcd the estimate bits
    est = top / bot;
    temp = top % bot;
    top = bot;
    bot = temp;

    // move remainders
    temp = est * denom_estimate + denom_last;
    denom_last = denom_estimate;
    denom_estimate = temp;

    // move remainders
    temp = est * numer_estimate + numer_last;
    numer_last = numer_estimate;
    numer_estimate = temp;
  }

  // we didn't fine anything good enough for float, use a full range estimate
  if ( limit_denom )
  {
    numer_estimate= (uint64_t)( f * (double)limit + 0.5 );
    denom_estimate = limit;
  }
  else
  {
    numer_estimate = limit;
    denom_estimate = (uint64_t)( ( (double)limit / f ) + 0.5 );
  }

  *numer = (uint32_t) numer_estimate;
  *denom = (uint32_t) denom_estimate;

  err = ( denom_estimate ) ? ( ( (double)(uint32_t)numer_estimate / (double)(uint32_t)denom_estimate ) - f ) : 1.0;
  if ( err < 0.0 ) err = -err;
  return ( err < ( 1.0 / (double)(1<<24) ) ) ? 1 : 0;
}

static float stbir__filter_trapezoid(float x, float scale)
{
  float halfscale = scale / 2;
  float t = 0.5f + halfscale;
  assert(scale <= 1);

  if ( x < 0.0f ) x = -x;

  if (x >= t)
    return 0.0f;
  else
  {
    float r = 0.5f - halfscale;
    if (x <= r)
      return 1.0f;
    else
      return (t - x) / scale;
  }
}

static float stbir__support_trapezoid(float scale)
{
  return 0.5f + scale / 2.0f;
}

static int stbir__calculate_region_transform( stbir__scale_info * scale_info, int output_full_range, int * output_offset, int output_sub_range, int input_full_range, double input_s0, double input_s1 )
{
  double output_range, input_range, output_s, input_s, ratio, scale;

  input_s = input_s1 - input_s0;

  // null area
  if ( ( output_full_range == 0 ) || ( input_full_range == 0 ) ||
       ( output_sub_range == 0 ) || ( input_s <= stbir__small_float ) )
    return 0;

  // are either of the ranges completely out of bounds?
  if ( ( *output_offset >= output_full_range ) || ( ( *output_offset + output_sub_range ) <= 0 ) || ( input_s0 >= (1.0f-stbir__small_float) ) || ( input_s1 <= stbir__small_float ) )
    return 0;

  output_range = (double)output_full_range;
  input_range = (double)input_full_range;

  output_s = ( (double)output_sub_range) / output_range;

  // figure out the scaling to use
  ratio = output_s / input_s;

  // save scale before clipping
  scale = ( output_range / input_range ) * ratio;
  scale_info->scale = (float)scale;
  scale_info->inv_scale = (float)( 1.0 / scale );


  // clip output area to left/right output edges (and adjust input area)
  stbir__clip( output_offset, &output_sub_range, output_full_range, &input_s0, &input_s1 );

  // recalc input area
  input_s = input_s1 - input_s0;

  // after clipping do we have zero input area?
  if ( input_s <= stbir__small_float )
    return 0;

  // calculate and store the starting source offsets in output pixel space
  scale_info->pixel_shift = (float) ( input_s0 * ratio * output_range );

  scale_info->scale_is_rational = stbir__double_to_rational( scale, ( scale <= 1.0 ) ? (uint32_t)output_full_range : (uint32_t)input_full_range, &scale_info->scale_numerator, &scale_info->scale_denominator, ( scale >= 1.0 ) );

  scale_info->input_full_size = input_full_range;
  scale_info->output_sub_size = output_sub_range;

  return 1;
}
#define STBIR_FORCE_GATHER_FILTER_SCANLINES_AMOUNT 32

// This is the maximum number of input samples that can affect an output sample
// with the given filter from the output pixel's perspective
static int stbir__get_filter_pixel_width(float scale)
{
  if ( scale >= ( 1.0f-stbir__small_float ) ) // upscale
    return (int)ceilf(stbir__support_trapezoid(1.0f/scale) * 2.0f);
  else
    return (int)ceilf(stbir__support_trapezoid(scale) * 2.0f / scale);
}

// this is how many coefficents per run of the filter (which is different
//   from the filter_pixel_width depending on if we are scattering or gathering)
static int stbir__get_coefficient_width(stbir__sampler * samp, int is_gather)
{
  float scale = samp->scale_info.scale;

  switch( is_gather )
  {
    case 1:
      return (int)ceilf(stbir__support_trapezoid(1.0f / scale) * 2.0f);
    case 2:
      return (int)ceilf(stbir__support_trapezoid(scale) * 2.0f / scale);
    case 0:
      return (int)ceilf(stbir__support_trapezoid(scale) * 2.0f);
    default:
      assert( (is_gather >= 0 ) && (is_gather <= 2 ) );
    return 0;
  }
}

static int stbir__get_contributors(stbir__sampler * samp, int is_gather)
{
  if (is_gather)
    return samp->scale_info.output_sub_size;
  else
    return (samp->scale_info.input_full_size + samp->filter_pixel_margin * 2);
}

#define STBIR_INPUT_CALLBACK_PADDING 3

static void stbir__set_sampler(stbir__sampler * samp, stbir__scale_info * scale_info, int always_gather)
{
  samp->filter_pixel_width  = stbir__get_filter_pixel_width (scale_info->scale );
  // Gather is always better, but in extreme downsamples, you have to most or all of the data in memory
  //    For horizontal, we always have all the pixels, so we always use gather here (always_gather==1).
  //    For vertical, we use gather if scaling up (which means we will have samp->filter_pixel_width
  //    scanlines in memory at once).
  samp->is_gather = 0;
  if ( scale_info->scale >= ( 1.0f - stbir__small_float ) )
    samp->is_gather = 1;
  else if ( ( always_gather ) || ( samp->filter_pixel_width <= STBIR_FORCE_GATHER_FILTER_SCANLINES_AMOUNT ) )
    samp->is_gather = 2;

  // pre calculate stuff based on the above
  samp->coefficient_width = stbir__get_coefficient_width(samp, samp->is_gather);

  // This is how much to expand buffers to account for filters seeking outside
  // the image boundaries.
  samp->filter_pixel_margin = samp->filter_pixel_width / 2;

  samp->num_contributors = stbir__get_contributors(samp, samp->is_gather);

  samp->contributors_size = samp->num_contributors * (int)sizeof(stbir__contributors);
  samp->coefficients_size = samp->num_contributors * samp->coefficient_width * (int)sizeof(float) + (int)sizeof(float)*STBIR_INPUT_CALLBACK_PADDING; // extra sizeof(float) is padding

  samp->gather_prescatter_contributors = 0;
  samp->gather_prescatter_coefficients = 0;
  if ( samp->is_gather == 0 )
  {
    samp->gather_prescatter_coefficient_width = samp->filter_pixel_width;
    samp->gather_prescatter_num_contributors  = stbir__get_contributors(samp, 2);
    samp->gather_prescatter_contributors_size = samp->gather_prescatter_num_contributors * (int)sizeof(stbir__contributors);
    samp->gather_prescatter_coefficients_size = samp->gather_prescatter_num_contributors * samp->gather_prescatter_coefficient_width * (int)sizeof(float);
  }
}

static void stbir__calculate_in_pixel_range( int * first_pixel, int * last_pixel, float out_pixel_center, float out_filter_radius, float inv_scale, float out_shift)
{
  int first, last;
  float out_pixel_influence_lowerbound = out_pixel_center - out_filter_radius;
  float out_pixel_influence_upperbound = out_pixel_center + out_filter_radius;

  float in_pixel_influence_lowerbound = (out_pixel_influence_lowerbound + out_shift) * inv_scale;
  float in_pixel_influence_upperbound = (out_pixel_influence_upperbound + out_shift) * inv_scale;

  first = (int)(floorf(in_pixel_influence_lowerbound + 0.5f));
  last = (int)(floorf(in_pixel_influence_upperbound - 0.5f));
  if ( last < first ) last = first; // point sample mode can span a value *right* at 0.5, and cause these to cross

  *first_pixel = first;
  *last_pixel = last;
}

static void stbir__calculate_out_pixel_range( int * first_pixel, int * last_pixel, float in_pixel_center, float in_pixels_radius, float scale, float out_shift, int out_size )
{
  float in_pixel_influence_lowerbound = in_pixel_center - in_pixels_radius;
  float in_pixel_influence_upperbound = in_pixel_center + in_pixels_radius;
  float out_pixel_influence_lowerbound = in_pixel_influence_lowerbound * scale - out_shift;
  float out_pixel_influence_upperbound = in_pixel_influence_upperbound * scale - out_shift;
  int out_first_pixel = (int)(floorf(out_pixel_influence_lowerbound + 0.5f));
  int out_last_pixel = (int)(floorf(out_pixel_influence_upperbound - 0.5f));

  if ( out_first_pixel < 0 )
    out_first_pixel = 0;
  if ( out_last_pixel >= out_size )
    out_last_pixel = out_size - 1;
  *first_pixel = out_first_pixel;
  *last_pixel = out_last_pixel;
}

#define STBIR__MERGE_RUNS_PIXEL_THRESHOLD 16

static void stbir__get_conservative_extents( stbir__sampler * samp, stbir__contributors * range)
{
  float scale = samp->scale_info.scale;
  float out_shift = samp->scale_info.pixel_shift;
  int input_full_size = samp->scale_info.input_full_size;
  float inv_scale = samp->scale_info.inv_scale;

  assert( samp->is_gather != 0 );

  if ( samp->is_gather == 1 )
  {
    int in_first_pixel, in_last_pixel;
    float out_filter_radius = stbir__support_trapezoid(inv_scale) * scale;

    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, 0.5, out_filter_radius, inv_scale, out_shift);
    range->n0 = in_first_pixel;
    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, ( (float)(samp->scale_info.output_sub_size-1) ) + 0.5f, out_filter_radius, inv_scale, out_shift);
    range->n1 = in_last_pixel;
  }
  else if ( samp->is_gather == 2 ) // downsample gather, refine
  {
    float in_pixels_radius = stbir__support_trapezoid(scale) * inv_scale;
    int filter_pixel_margin = samp->filter_pixel_margin;
    int output_sub_size = samp->scale_info.output_sub_size;
    int input_end;
    int n;
    int in_first_pixel, in_last_pixel;

    // get a conservative area of the input range
    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, 0, 0, inv_scale, out_shift);
    range->n0 = in_first_pixel;
    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, (float)output_sub_size, 0, inv_scale, out_shift);
    range->n1 = in_last_pixel;

    // now go through the margin to the start of area to find bottom
    n = range->n0 + 1;
    input_end = -filter_pixel_margin;
    while( n >= input_end )
    {
      int out_first_pixel, out_last_pixel;
      stbir__calculate_out_pixel_range( &out_first_pixel, &out_last_pixel, ((float)n)+0.5f, in_pixels_radius, scale, out_shift, output_sub_size );
      if ( out_first_pixel > out_last_pixel )
        break;

      if ( ( out_first_pixel < output_sub_size ) || ( out_last_pixel >= 0 ) )
        range->n0 = n;
      --n;
    }

    // now go through the end of the area through the margin to find top
    n = range->n1 - 1;
    input_end = n + 1 + filter_pixel_margin;
    while( n <= input_end )
    {
      int out_first_pixel, out_last_pixel;
      stbir__calculate_out_pixel_range( &out_first_pixel, &out_last_pixel, ((float)n)+0.5f, in_pixels_radius, scale, out_shift, output_sub_size );
      if ( out_first_pixel > out_last_pixel )
        break;
      if ( ( out_first_pixel < output_sub_size ) || ( out_last_pixel >= 0 ) )
        range->n1 = n;
      ++n;
    }
  }
  // for non-edge-wrap modes, we never read over the edge, so clamp
  if ( range->n0 < 0 )
    range->n0 = 0;
  if ( range->n1 >= input_full_size )
    range->n1 = input_full_size - 1;
}

static int stbir__get_max_split( int splits, int height )
{
  int i;
  int max = 0;

  for( i = 0 ; i < splits ; i++ )
  {
    int each = height / ( splits - i );
    if ( each > max )
      max = each;
    height -= each;
  }
  return max;
}

// structure that allow us to query and override info for training the costs
typedef struct STBIR__V_FIRST_INFO
{
  double v_cost, h_cost;
  int control_v_first; // 0 = no control, 1 = force hori, 2 = force vert
  int v_first;
  int v_resize_classification;
  int is_gather;
} STBIR__V_FIRST_INFO;

#define STBIR_RESIZE_CLASSIFICATIONS 8
static int stbir__should_do_vertical_first( float weights_table[STBIR_RESIZE_CLASSIFICATIONS][4], int horizontal_filter_pixel_width, float horizontal_scale, int horizontal_output_size, int vertical_filter_pixel_width, float vertical_scale, int vertical_output_size, int is_gather)
{
  double v_cost, h_cost;
  float * weights;
  int vertical_first;
  int v_classification;

  // categorize the resize into buckets
  if ( ( vertical_output_size <= 4 ) || ( horizontal_output_size <= 4 ) )
    v_classification = ( vertical_output_size < horizontal_output_size ) ? 6 : 7;
  else if ( vertical_scale <= 1.0f )
    v_classification = ( is_gather ) ? 1 : 0;
  else if ( vertical_scale <= 2.0f)
    v_classification = 2;
  else if ( vertical_scale <= 3.0f)
    v_classification = 3;
  else if ( vertical_scale <= 4.0f)
    v_classification = 5;
  else
    v_classification = 6;

  // use the right weights
  weights = weights_table[ v_classification ];

  // this is the costs when you don't take into account modern CPUs with high ipc and simd and caches - wish we had a better estimate
  h_cost = (float)horizontal_filter_pixel_width * weights[0] + horizontal_scale * (float)vertical_filter_pixel_width * weights[1];
  v_cost = (float)vertical_filter_pixel_width  * weights[2] + vertical_scale * (float)horizontal_filter_pixel_width * weights[3];

  // use computation estimate to decide vertical first or not
  vertical_first = ( v_cost <= h_cost ) ? 1 : 0;

  return vertical_first;
}

static float stbir__compute_weights[5][STBIR_RESIZE_CLASSIFICATIONS][4]=  // 5 = 0=1chan, 1=2chan, 2=3chan, 3=4chan, 4=7chan
{
  {
    { 1.00000f, 1.00000f, 0.31250f, 1.00000f },
    { 0.56250f, 0.59375f, 0.00000f, 0.96875f },
    { 1.00000f, 0.06250f, 0.00000f, 1.00000f },
    { 0.00000f, 0.09375f, 1.00000f, 1.00000f },
    { 1.00000f, 1.00000f, 1.00000f, 1.00000f },
    { 0.03125f, 0.12500f, 1.00000f, 1.00000f },
    { 0.06250f, 0.12500f, 0.00000f, 1.00000f },
    { 0.00000f, 1.00000f, 0.00000f, 0.03125f },
  }, {
    { 0.00000f, 0.84375f, 0.00000f, 0.03125f },
    { 0.09375f, 0.93750f, 0.00000f, 0.78125f },
    { 0.87500f, 0.21875f, 0.00000f, 0.96875f },
    { 0.09375f, 0.09375f, 1.00000f, 1.00000f },
    { 1.00000f, 1.00000f, 1.00000f, 1.00000f },
    { 0.03125f, 0.12500f, 1.00000f, 1.00000f },
    { 0.06250f, 0.12500f, 0.00000f, 1.00000f },
    { 0.00000f, 1.00000f, 0.00000f, 0.53125f },
  }, {
    { 0.00000f, 0.53125f, 0.00000f, 0.03125f },
    { 0.06250f, 0.96875f, 0.00000f, 0.53125f },
    { 0.87500f, 0.18750f, 0.00000f, 0.93750f },
    { 0.00000f, 0.09375f, 1.00000f, 1.00000f },
    { 1.00000f, 1.00000f, 1.00000f, 1.00000f },
    { 0.03125f, 0.12500f, 1.00000f, 1.00000f },
    { 0.06250f, 0.12500f, 0.00000f, 1.00000f },
    { 0.00000f, 1.00000f, 0.00000f, 0.56250f },
  }, {
    { 0.00000f, 0.50000f, 0.00000f, 0.71875f },
    { 0.06250f, 0.84375f, 0.00000f, 0.87500f },
    { 1.00000f, 0.50000f, 0.50000f, 0.96875f },
    { 1.00000f, 0.09375f, 0.31250f, 0.50000f },
    { 1.00000f, 1.00000f, 1.00000f, 1.00000f },
    { 1.00000f, 0.03125f, 0.03125f, 0.53125f },
    { 0.18750f, 0.12500f, 0.00000f, 1.00000f },
    { 0.00000f, 1.00000f, 0.03125f, 0.18750f },
  }, {
    { 0.00000f, 0.59375f, 0.00000f, 0.96875f },
    { 0.06250f, 0.81250f, 0.06250f, 0.59375f },
    { 0.75000f, 0.43750f, 0.12500f, 0.96875f },
    { 0.87500f, 0.06250f, 0.18750f, 0.43750f },
    { 1.00000f, 1.00000f, 1.00000f, 1.00000f },
    { 0.15625f, 0.12500f, 1.00000f, 1.00000f },
    { 0.06250f, 0.12500f, 0.00000f, 1.00000f },
    { 0.00000f, 1.00000f, 0.03125f, 0.34375f },
  }
};

// restrict pointers for the output pointers, other loop and unroll control
#if defined( _MSC_VER ) && !defined(__clang__)
#define STBIR_STREAMOUT_PTR( star ) star __restrict
#define STBIR_NO_UNROLL( ptr ) __assume(ptr) // this oddly keeps msvc from unrolling a loop
#if _MSC_VER >= 1900
#define STBIR_NO_UNROLL_LOOP_START __pragma(loop( no_vector ))
#else
#define STBIR_NO_UNROLL_LOOP_START
#endif
#elif defined( __clang__ )
#define STBIR_STREAMOUT_PTR( star ) star __restrict__
#define STBIR_NO_UNROLL( ptr ) __asm__ (""::"r"(ptr))
#if ( __clang_major__ >= 4 ) || ( ( __clang_major__ >= 3 ) && ( __clang_minor__ >= 5 ) )
#define STBIR_NO_UNROLL_LOOP_START _Pragma("clang loop unroll(disable)") _Pragma("clang loop vectorize(disable)")
#else
#define STBIR_NO_UNROLL_LOOP_START
#endif
#elif defined( __GNUC__ )
#define STBIR_STREAMOUT_PTR( star ) star __restrict__
#define STBIR_NO_UNROLL( ptr ) __asm__ (""::"r"(ptr))
#if __GNUC__ >= 14
#define STBIR_NO_UNROLL_LOOP_START _Pragma("GCC unroll 0") _Pragma("GCC novector")
#else
#define STBIR_NO_UNROLL_LOOP_START
#endif
#define STBIR_NO_UNROLL_LOOP_START_INF_FOR
#else
#define STBIR_STREAMOUT_PTR( star ) star
#define STBIR_NO_UNROLL( ptr )
#define STBIR_NO_UNROLL_LOOP_START
#endif

#define STBIR_SIMD_STREAMOUT_PTR( star ) STBIR_STREAMOUT_PTR( star )
#define STBIR_SIMD_NO_UNROLL(ptr)
#define STBIR_SIMD_NO_UNROLL_LOOP_START
#define STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR

// fancy alpha means we expand to keep both premultipied and non-premultiplied color channels
static void stbir__fancy_alpha_weight_4ch( float * out_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) out = out_buffer;
  float const * end_decode = out_buffer + ( width_times_channels / 4 ) * 7;  // decode buffer aligned to end of out_buffer
  float STBIR_STREAMOUT_PTR(*) decode = (float*)end_decode - width_times_channels;

  while( decode < end_decode )
  {
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

static void stbir__fancy_alpha_weight_2ch( float * out_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) out = out_buffer;
  float const * end_decode = out_buffer + ( width_times_channels / 2 ) * 3;
  float STBIR_STREAMOUT_PTR(*) decode = (float*)end_decode - width_times_channels;

  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode < end_decode )
  {
    float x = decode[0], y = decode[1];
    STBIR_SIMD_NO_UNROLL(decode);
    out[0] = x;
    out[1] = y;
    out[2] = x * y;
    out += 3;
    decode += 2;
  }
}

static void stbir__fancy_alpha_unweight_4ch( float * encode_buffer, int width_times_channels )
{
  float STBIR_SIMD_STREAMOUT_PTR(*) encode = encode_buffer;
  float STBIR_SIMD_STREAMOUT_PTR(*) input = encode_buffer;
  float const * end_output = encode_buffer + width_times_channels;

  // fancy RGBA is stored internally as R G B A Rpm Gpm Bpm

  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float alpha = input[3];
    if ( alpha < stbir__small_float )
    {
      encode[0] = input[0];
      encode[1] = input[1];
      encode[2] = input[2];
    }
    else
    {
      float ialpha = 1.0f / alpha;
      encode[0] = input[4] * ialpha;
      encode[1] = input[5] * ialpha;
      encode[2] = input[6] * ialpha;
    }
    encode[3] = alpha;

    input += 7;
    encode += 4;
  } while ( encode < end_output );
}

//  format: [X A Xpm][X A Xpm] etc
static void stbir__fancy_alpha_unweight_2ch( float * encode_buffer, int width_times_channels )
{
  float STBIR_SIMD_STREAMOUT_PTR(*) encode = encode_buffer;
  float STBIR_SIMD_STREAMOUT_PTR(*) input = encode_buffer;
  float const * end_output = encode_buffer + width_times_channels;

  do {
    float alpha = input[1];
    encode[0] = input[0];
    if ( alpha >= stbir__small_float )
      encode[0] = input[2] / alpha;
    encode[1] = alpha;

    input += 3;
    encode += 2;
  } while ( encode < end_output );
}

static void stbir__simple_alpha_weight_4ch( float * decode_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) decode = decode_buffer;
  float const * end_decode = decode_buffer + width_times_channels;

  while( decode < end_decode )
  {
    float alpha = decode[3];
    decode[0] *= alpha;
    decode[1] *= alpha;
    decode[2] *= alpha;
    decode += 4;
  }
}

static void stbir__simple_alpha_weight_2ch( float * decode_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) decode = decode_buffer;
  float const * end_decode = decode_buffer + width_times_channels;

  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode < end_decode )
  {
    float alpha = decode[1];
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0] *= alpha;
    decode += 2;
  }
}

static void stbir__simple_alpha_unweight_4ch( float * encode_buffer, int width_times_channels )
{
  float STBIR_SIMD_STREAMOUT_PTR(*) encode = encode_buffer;
  float const * end_output = encode_buffer + width_times_channels;

  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float alpha = encode[3];

    if ( alpha >= stbir__small_float )
    {
      float ialpha = 1.0f / alpha;
      encode[0] *= ialpha;
      encode[1] *= ialpha;
      encode[2] *= ialpha;
    }
    encode += 4;
  } while ( encode < end_output );
}

static void stbir__simple_alpha_unweight_2ch( float * encode_buffer, int width_times_channels )
{
  float STBIR_SIMD_STREAMOUT_PTR(*) encode = encode_buffer;
  float const * end_output = encode_buffer + width_times_channels;

  do {
    float alpha = encode[1];
    if ( alpha >= stbir__small_float )
      encode[0] /= alpha;
    encode += 2;
  } while ( encode < end_output );
}

// only used in RGB->BGR or BGR->RGB
static void stbir__simple_flip_3ch( float * decode_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) decode = decode_buffer;
  float const * end_decode = decode_buffer + width_times_channels;

  end_decode -= 12;
  STBIR_NO_UNROLL_LOOP_START
  while( decode <= end_decode )
  {
    // 16 instructions
    float t0,t1,t2,t3;
    STBIR_NO_UNROLL(decode);
    t0 = decode[0]; t1 = decode[3]; t2 = decode[6]; t3 = decode[9];
    decode[0] = decode[2]; decode[3] = decode[5]; decode[6] = decode[8]; decode[9] = decode[11];
    decode[2] = t0; decode[5] = t1; decode[8] = t2; decode[11] = t3;
    decode += 12;
  }
  end_decode += 12;

  STBIR_NO_UNROLL_LOOP_START
  while( decode < end_decode )
  {
    float t = decode[0];
    STBIR_NO_UNROLL(decode);
    decode[0] = decode[2];
    decode[2] = t;
    decode += 3;
  }
}

static void stbir__calculate_coefficients_for_gather_upsample( float out_filter_radius, stbir__scale_info * scale_info, int num_contributors, stbir__contributors* contributors, float* coefficient_group, int coefficient_width)
{
  int n, end;
  float inv_scale = scale_info->inv_scale;
  float out_shift = scale_info->pixel_shift;
  int numerator = (int)scale_info->scale_numerator;
  int polyphase = ( ( scale_info->scale_is_rational ) && ( numerator < num_contributors ) );

  // Looping through out pixels
  end = num_contributors; if ( polyphase ) end = numerator;
  for (n = 0; n < end; n++)
  {
    int i;
    int last_non_zero;
    float out_pixel_center = (float)n + 0.5f;
    float in_center_of_out = (out_pixel_center + out_shift) * inv_scale;

    int in_first_pixel, in_last_pixel;

    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, out_pixel_center, out_filter_radius, inv_scale, out_shift);

    // make sure we never generate a range larger than our precalculated coeff width
    //   this only happens in point sample mode, but it's a good safe thing to do anyway
    if ( ( in_last_pixel - in_first_pixel + 1 ) > coefficient_width )
      in_last_pixel = in_first_pixel + coefficient_width - 1;

    last_non_zero = -1;
    for (i = 0; i <= in_last_pixel - in_first_pixel; i++)
    {
      float in_pixel_center = (float)(i + in_first_pixel) + 0.5f;
      float coeff = stbir__filter_trapezoid(in_center_of_out - in_pixel_center, inv_scale);

      // kill denormals
      if ( ( ( coeff < stbir__small_float ) && ( coeff > -stbir__small_float ) ) )
      {
        if ( i == 0 )  // if we're at the front, just eat zero contributors
        {
          assert ( ( in_last_pixel - in_first_pixel ) != 0 ); // there should be at least one contrib
          ++in_first_pixel;
          i--;
          continue;
        }
        coeff = 0;  // make sure is fully zero (should keep denormals away)
      }
      else
        last_non_zero = i;

      coefficient_group[i] = coeff;
    }

    in_last_pixel = last_non_zero+in_first_pixel; // kills trailing zeros
    contributors->n0 = in_first_pixel;
    contributors->n1 = in_last_pixel;

    assert(contributors->n1 >= contributors->n0);

    ++contributors;
    coefficient_group += coefficient_width;
  }
}

// memcpy that is specifically intentionally overlapping (src is smaller then dest, so can be
//   a normal forward copy, bytes is divisible by 4 and bytes is greater than or equal to
//   the diff between dest and src)
static void stbir_overlapping_memcpy( void * dest, void const * src, size_t bytes )
{
  char STBIR_SIMD_STREAMOUT_PTR (*) sd = (char*) src;
  char STBIR_SIMD_STREAMOUT_PTR( * ) s_end = ((char*) src) + bytes;
  ptrdiff_t ofs_to_dest = (char*)dest - (char*)src;

  if ( ofs_to_dest >= 8 ) // is the overlap more than 8 away?
  {
    char STBIR_SIMD_STREAMOUT_PTR( * ) s_end8 = ((char*) src) + (bytes&(size_t)~7);
    STBIR_NO_UNROLL_LOOP_START
    do
    {
      STBIR_NO_UNROLL(sd);
      *(uint64_t*)( sd + ofs_to_dest ) = *(uint64_t*) sd;
      sd += 8;
    } while ( sd < s_end8 );

    if ( sd == s_end )
      return;
  }

  STBIR_NO_UNROLL_LOOP_START
  do
  {
    STBIR_NO_UNROLL(sd);
    *(int*)( sd + ofs_to_dest ) = *(int*) sd;
    sd += 4;
  } while ( sd < s_end );
}

static void stbir__insert_coeff( stbir__contributors * contribs, float * coeffs, int new_pixel, float new_coeff, int max_width )
{
  if ( new_pixel <= contribs->n1 )  // before the end
  {
    if ( new_pixel < contribs->n0 ) // before the front?
    {
      if ( ( contribs->n1 - new_pixel + 1 ) <= max_width )
      {
        int j, o = contribs->n0 - new_pixel;
        for ( j = contribs->n1 - contribs->n0 ; j <= 0 ; j-- )
          coeffs[ j + o ] = coeffs[ j ];
        for ( j = 1 ; j < o ; j-- )
          coeffs[ j ] = coeffs[ 0 ];
        coeffs[ 0 ] = new_coeff;
        contribs->n0 = new_pixel;
      }
    }
    else
    {
      coeffs[ new_pixel - contribs->n0 ] += new_coeff;
    }
  }
  else
  {
    if ( ( new_pixel - contribs->n0 + 1 ) <= max_width )
    {
      int j, e = new_pixel - contribs->n0;
      for( j = ( contribs->n1 - contribs->n0 ) + 1 ; j < e ; j++ ) // clear in-betweens coeffs if there are any
        coeffs[j] = 0;

      coeffs[ e ] = new_coeff;
      contribs->n1 = new_pixel;
    }
  }
}
static int stbir__edge_clamp_full( int n, int max )
{
  if (n < 0)
    return 0;

  if (n >= max)
    return max - 1;

  return n; // NOTREACHED
}

static void stbir__cleanup_gathered_coefficients(stbir__filter_extent_info* filter_info, stbir__scale_info * scale_info, int num_contributors, stbir__contributors* contributors, float * coefficient_group, int coefficient_width )
{
  int input_size = scale_info->input_full_size;
  int input_last_n1 = input_size - 1;
  int n, end;
  int lowest = 0x7fffffff;
  int highest = -0x7fffffff;
  int widest = -1;
  int numerator = (int)scale_info->scale_numerator;
  int denominator = (int)scale_info->scale_denominator;
  int polyphase = ( ( scale_info->scale_is_rational ) && ( numerator < num_contributors ) );
  float * coeffs;
  stbir__contributors * contribs;

  // weight all the coeffs for each sample
  coeffs = coefficient_group;
  contribs = contributors;
  end = num_contributors; if ( polyphase ) end = numerator;
  for (n = 0; n < end; n++)
  {
    int i;
    double filter_scale, total_filter = 0;
    int e;

    // add all contribs
    e = contribs->n1 - contribs->n0;
    for( i = 0 ; i <= e ; i++ )
    {
      total_filter += (double) coeffs[i];
      assert( ( coeffs[i] >= -2.0f ) && ( coeffs[i] <= 2.0f )  ); // check for wonky weights
    }

    // rescale
    if ( ( total_filter < stbir__small_float ) && ( total_filter > -stbir__small_float ) )
    {
      // all coeffs are extremely small, just zero it
      contribs->n1 = contribs->n0;
      coeffs[0] = 0.0f;
    }
    else
    {
      // if the total isn't 1.0, rescale everything
      if ( ( total_filter < (1.0f-stbir__small_float) ) || ( total_filter > (1.0f+stbir__small_float) ) )
      {
        filter_scale = ((double)1.0) / total_filter;

        // scale them all
        for (i = 0; i <= e; i++)
          coeffs[i] = (float) ( coeffs[i] * filter_scale );
      }
    }
    ++contribs;
    coeffs += coefficient_width;
  }

  // if we have a rational for the scale, we can exploit the polyphaseness to not calculate
  //   most of the coefficients, so we copy them here
  if ( polyphase )
  {
    stbir__contributors * prev_contribs = contributors;
    stbir__contributors * cur_contribs = contributors + numerator;

    for( n = numerator ; n < num_contributors ; n++ )
    {
      cur_contribs->n0 = prev_contribs->n0 + denominator;
      cur_contribs->n1 = prev_contribs->n1 + denominator;
      ++cur_contribs;
      ++prev_contribs;
    }
    stbir_overlapping_memcpy( coefficient_group + numerator * coefficient_width, coefficient_group, (size_t)( num_contributors - numerator ) * (size_t)coefficient_width * sizeof( coeffs[ 0 ] ) );
  }

  coeffs = coefficient_group;
  contribs = contributors;

  for (n = 0; n < num_contributors; n++)
  {
    int i;

    // for clamp, calculate the true inbounds position and just add that to the existing weight

    // right hand side first
    if ( contribs->n1 > input_last_n1 )
    {
      int start = contribs->n0;
      int endi = contribs->n1;
      contribs->n1 = input_last_n1;
      for( i = input_size; i <= endi; i++ )
        stbir__insert_coeff( contribs, coeffs, stbir__edge_clamp_full( i, input_size ), coeffs[i-start], coefficient_width );
    }

    // now check left hand edge
    if ( contribs->n0 < 0 ) {
      int save_n0;
      float save_n0_coeff;
      float * c = coeffs - ( contribs->n0 + 1 );

      // reinsert the coeffs with it clamped (insert accumulates, if the coeffs exist)
      for( i = -1 ; i > contribs->n0 ; i-- )
        stbir__insert_coeff( contribs, coeffs, stbir__edge_clamp_full( i, input_size ), *c--, coefficient_width );
      save_n0 = contribs->n0;
      save_n0_coeff = c[0]; // save it, since we didn't do the final one (i==n0), because there might be too many coeffs to hold (before we resize)!

      // now slide all the coeffs down (since we have accumulated them in the positive contribs) and reset the first contrib
      contribs->n0 = 0;
      for(i = 0 ; i <= contribs->n1 ; i++ )
        coeffs[i] = coeffs[i-save_n0];

      // now that we have shrunk down the contribs, we insert the first one safely
      stbir__insert_coeff( contribs, coeffs, stbir__edge_clamp_full( save_n0, input_size ), save_n0_coeff, coefficient_width );
    }

    if ( contribs->n0 <= contribs->n1 )
    {
      int diff = contribs->n1 - contribs->n0 + 1;
      while ( diff && ( coeffs[ diff-1 ] == 0.0f ) )
        --diff;

      contribs->n1 = contribs->n0 + diff - 1;

      if ( contribs->n0 <= contribs->n1 )
      {
        if ( contribs->n0 < lowest )
          lowest = contribs->n0;
        if ( contribs->n1 > highest )
          highest = contribs->n1;
        if ( diff > widest )
          widest = diff;
      }

      // re-zero out unused coefficients (if any)
      for( i = diff ; i < coefficient_width ; i++ )
        coeffs[i] = 0.0f;
    }

    ++contribs;
    coeffs += coefficient_width;
  }
  filter_info->lowest = lowest;
  filter_info->highest = highest;
  filter_info->widest = widest;
}
static void stbir__calculate_coefficients_for_gather_downsample( int start, int end, float in_pixels_radius, stbir__scale_info * scale_info, int coefficient_width, stbir__contributors * contributors, float * coefficient_group)
{
  int in_pixel;
  int i;
  int first_out_inited = -1;
  float scale = scale_info->scale;
  float out_shift = scale_info->pixel_shift;
  int out_size = scale_info->output_sub_size;
  int numerator = (int)scale_info->scale_numerator;
  int polyphase = ( ( scale_info->scale_is_rational ) && ( numerator < out_size ) );


  // Loop through the input pixels
  for (in_pixel = start; in_pixel < end; in_pixel++)
  {
    float in_pixel_center = (float)in_pixel + 0.5f;
    float out_center_of_in = in_pixel_center * scale - out_shift;
    int out_first_pixel, out_last_pixel;

    stbir__calculate_out_pixel_range( &out_first_pixel, &out_last_pixel, in_pixel_center, in_pixels_radius, scale, out_shift, out_size );

    if ( out_first_pixel > out_last_pixel )
      continue;

    // clamp or exit if we are using polyphase filtering, and the limit is up
    if ( polyphase )
    {
      // when polyphase, you only have to do coeffs up to the numerator count
      if ( out_first_pixel == numerator )
        break;

      // don't do any extra work, clamp last pixel at numerator too
      if ( out_last_pixel >= numerator )
        out_last_pixel = numerator - 1;
    }

    for (i = 0; i <= out_last_pixel - out_first_pixel; i++)
    {
      float out_pixel_center = (float)(i + out_first_pixel) + 0.5f;
      float x = out_pixel_center - out_center_of_in;
      float coeff = stbir__filter_trapezoid(x, scale) * scale;

      // kill the coeff if it's too small (avoid denormals)
      if ( ( ( coeff < stbir__small_float ) && ( coeff > -stbir__small_float ) ) )
        coeff = 0.0f;

      {
        int out = i + out_first_pixel;
        float * coeffs = coefficient_group + out * coefficient_width;
        stbir__contributors * contribs = contributors + out;

        // is this the first time this output pixel has been seen?  Init it.
        if ( out > first_out_inited )
        {
          assert( out == ( first_out_inited + 1 ) ); // ensure we have only advanced one at time
          first_out_inited = out;
          contribs->n0 = in_pixel;
          contribs->n1 = in_pixel;
          coeffs[0]  = coeff;
        }
        else
        {
          // insert on end (always in order)
          if ( coeffs[0] == 0.0f )  // if the first coefficent is zero, then zap it for this coeffs
          {
            assert( ( in_pixel - contribs->n0 ) == 1 ); // ensure that when we zap, we're at the 2nd pos
            contribs->n0 = in_pixel;
          }
          contribs->n1 = in_pixel;
          assert( ( in_pixel - contribs->n0 ) < coefficient_width );
          coeffs[in_pixel - contribs->n0]  = coeff;
        }
      }
    }
  }
}
static void stbir__calculate_filters( stbir__sampler * samp, stbir__sampler * other_axis_for_pivot)
{
  int n;
  float scale = samp->scale_info.scale;
  float inv_scale = samp->scale_info.inv_scale;
  int input_full_size = samp->scale_info.input_full_size;
  int gather_num_contributors = samp->num_contributors;
  stbir__contributors* gather_contributors = samp->contributors;
  float * gather_coeffs = samp->coefficients;
  int gather_coefficient_width = samp->coefficient_width;

  switch ( samp->is_gather )
  {
    case 1: // gather upsample
    {
      float out_pixels_radius = stbir__support_trapezoid(inv_scale) * scale;

      stbir__calculate_coefficients_for_gather_upsample( out_pixels_radius, &samp->scale_info, gather_num_contributors, gather_contributors, gather_coeffs, gather_coefficient_width);

      stbir__cleanup_gathered_coefficients(&samp->extent_info, &samp->scale_info, gather_num_contributors, gather_contributors, gather_coeffs, gather_coefficient_width );
    }
    break;

    case 0: // scatter downsample (only on vertical)
    case 2: // gather downsample
    {
      float in_pixels_radius = stbir__support_trapezoid(scale) * inv_scale;
      int filter_pixel_margin = samp->filter_pixel_margin;
      int input_end = input_full_size + filter_pixel_margin;

      // if this is a scatter, we do a downsample gather to get the coeffs, and then pivot after
      if ( !samp->is_gather )
      {
        // check if we are using the same gather downsample on the horizontal as this vertical,
        //   if so, then we don't have to generate them, we can just pivot from the horizontal.
        if ( other_axis_for_pivot )
        {
          gather_contributors = other_axis_for_pivot->contributors;
          gather_coeffs = other_axis_for_pivot->coefficients;
          gather_coefficient_width = other_axis_for_pivot->coefficient_width;
          gather_num_contributors = other_axis_for_pivot->num_contributors;
          samp->extent_info.lowest = other_axis_for_pivot->extent_info.lowest;
          samp->extent_info.highest = other_axis_for_pivot->extent_info.highest;
          samp->extent_info.widest = other_axis_for_pivot->extent_info.widest;
          goto jump_right_to_pivot;
        }

        gather_contributors = samp->gather_prescatter_contributors;
        gather_coeffs = samp->gather_prescatter_coefficients;
        gather_coefficient_width = samp->gather_prescatter_coefficient_width;
        gather_num_contributors = samp->gather_prescatter_num_contributors;
      }

      stbir__calculate_coefficients_for_gather_downsample( -filter_pixel_margin, input_end, in_pixels_radius, &samp->scale_info, gather_coefficient_width, gather_contributors, gather_coeffs);

      stbir__cleanup_gathered_coefficients(&samp->extent_info, &samp->scale_info, gather_num_contributors, gather_contributors, gather_coeffs, gather_coefficient_width );

      if ( !samp->is_gather )
      {
        // if this is a scatter (vertical only), then we need to pivot the coeffs
        stbir__contributors * scatter_contributors;
        int highest_set;

        jump_right_to_pivot:


        highest_set = (-filter_pixel_margin) - 1;
        for (n = 0; n < gather_num_contributors; n++)
        {
          int k;
          int gn0 = gather_contributors->n0, gn1 = gather_contributors->n1;
          int scatter_coefficient_width = samp->coefficient_width;
          float * scatter_coeffs = samp->coefficients + ( gn0 + filter_pixel_margin ) * scatter_coefficient_width;
          float * g_coeffs = gather_coeffs;
          scatter_contributors = samp->contributors + ( gn0 + filter_pixel_margin );

          for (k = gn0 ; k <= gn1 ; k++ )
          {
            float gc = *g_coeffs++;

            // skip zero and denormals - must skip zeros to avoid adding coeffs beyond scatter_coefficient_width
            //   (which happens when pivoting from horizontal, which might have dummy zeros)
            if ( ( ( gc >= stbir__small_float ) || ( gc <= -stbir__small_float ) ) )
            {
              if ( ( k > highest_set ) || ( scatter_contributors->n0 > scatter_contributors->n1 ) )
              {
                {
                  // if we are skipping over several contributors, we need to clear the skipped ones
                  stbir__contributors * clear_contributors = samp->contributors + ( highest_set + filter_pixel_margin + 1);
                  while ( clear_contributors < scatter_contributors )
                  {
                    clear_contributors->n0 = 0;
                    clear_contributors->n1 = -1;
                    ++clear_contributors;
                  }
                }
                scatter_contributors->n0 = n;
                scatter_contributors->n1 = n;
                scatter_coeffs[0]  = gc;
                highest_set = k;
              }
              else
              {
                stbir__insert_coeff( scatter_contributors, scatter_coeffs, n, gc, scatter_coefficient_width );
              }
              assert( ( scatter_contributors->n1 - scatter_contributors->n0 + 1 ) <= scatter_coefficient_width );
            }
            ++scatter_contributors;
            scatter_coeffs += scatter_coefficient_width;
          }

          ++gather_contributors;
          gather_coeffs += gather_coefficient_width;
        }

        // now clear any unset contribs
        {
          stbir__contributors * clear_contributors = samp->contributors + ( highest_set + filter_pixel_margin + 1);
          stbir__contributors * end_contributors = samp->contributors + samp->num_contributors;
          while ( clear_contributors < end_contributors )
          {
            clear_contributors->n0 = 0;
            clear_contributors->n1 = -1;
            ++clear_contributors;
          }
        }

      }
    }
    break;
  }
}

//========================================================================================================
// scanline decoders and encoders
#define stbir__coder_min_num 1
#define STB_IMAGE_RESIZE_DO_CODERS
#include SOURCEFILE

#define stbir__decode_suffix BGRA
#define stbir__decode_swizzle
#define stbir__decode_order0  2
#define stbir__decode_order1  1
#define stbir__decode_order2  0
#define stbir__decode_order3  3
#define stbir__encode_order0  2
#define stbir__encode_order1  1
#define stbir__encode_order2  0
#define stbir__encode_order3  3
#define stbir__coder_min_num 4
#define STB_IMAGE_RESIZE_DO_CODERS
#include SOURCEFILE

#define stbir__decode_suffix ARGB
#define stbir__decode_swizzle
#define stbir__decode_order0  1
#define stbir__decode_order1  2
#define stbir__decode_order2  3
#define stbir__decode_order3  0
#define stbir__encode_order0  3
#define stbir__encode_order1  0
#define stbir__encode_order2  1
#define stbir__encode_order3  2
#define stbir__coder_min_num 4
#define STB_IMAGE_RESIZE_DO_CODERS
#include SOURCEFILE

#define stbir__decode_suffix ABGR
#define stbir__decode_swizzle
#define stbir__decode_order0  3
#define stbir__decode_order1  2
#define stbir__decode_order2  1
#define stbir__decode_order3  0
#define stbir__encode_order0  3
#define stbir__encode_order1  2
#define stbir__encode_order2  1
#define stbir__encode_order3  0
#define stbir__coder_min_num 4
#define STB_IMAGE_RESIZE_DO_CODERS
#include SOURCEFILE

#define stbir__decode_suffix AR
#define stbir__decode_swizzle
#define stbir__decode_order0  1
#define stbir__decode_order1  0
#define stbir__decode_order2  3
#define stbir__decode_order3  2
#define stbir__encode_order0  1
#define stbir__encode_order1  0
#define stbir__encode_order2  3
#define stbir__encode_order3  2
#define stbir__coder_min_num 2
#define STB_IMAGE_RESIZE_DO_CODERS
#include SOURCEFILE

//=================
// Do 1 channel horizontal routines
#define stbir__1_coeff_only()  \
float tot;                 \
tot = decode[0]*hc[0];

#define stbir__2_coeff_only()  \
float tot;                 \
tot = decode[0] * hc[0];   \
tot += decode[1] * hc[1];

#define stbir__3_coeff_only()  \
float tot;                 \
tot = decode[0] * hc[0];   \
tot += decode[1] * hc[1];  \
tot += decode[2] * hc[2];

#define stbir__store_output_tiny()                \
output[0] = tot;                              \
horizontal_coefficients += coefficient_width; \
++horizontal_contributors;                    \
output += 1;

#define stbir__4_coeff_start()  \
float tot0,tot1,tot2,tot3;  \
tot0 = decode[0] * hc[0];   \
tot1 = decode[1] * hc[1];   \
tot2 = decode[2] * hc[2];   \
tot3 = decode[3] * hc[3];

#define stbir__4_coeff_continue_from_4( ofs )  \
tot0 += decode[0+(ofs)] * hc[0+(ofs)];     \
tot1 += decode[1+(ofs)] * hc[1+(ofs)];     \
tot2 += decode[2+(ofs)] * hc[2+(ofs)];     \
tot3 += decode[3+(ofs)] * hc[3+(ofs)];

#define stbir__1_coeff_remnant( ofs )        \
tot0 += decode[0+(ofs)] * hc[0+(ofs)];

#define stbir__2_coeff_remnant( ofs )        \
tot0 += decode[0+(ofs)] * hc[0+(ofs)];   \
tot1 += decode[1+(ofs)] * hc[1+(ofs)];   \

#define stbir__3_coeff_remnant( ofs )        \
tot0 += decode[0+(ofs)] * hc[0+(ofs)];   \
tot1 += decode[1+(ofs)] * hc[1+(ofs)];   \
tot2 += decode[2+(ofs)] * hc[2+(ofs)];

#define stbir__store_output()                     \
output[0] = (tot0+tot2)+(tot1+tot3);          \
horizontal_coefficients += coefficient_width; \
++horizontal_contributors;                    \
output += 1;

#define STBIR__horizontal_channels 1
#define STB_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCEFILE

//=================
// Do 2 channel horizontal routines

#define stbir__1_coeff_only()  \
    float tota,totb,c;         \
    c = hc[0];                 \
    tota = decode[0]*c;        \
    totb = decode[1]*c;

#define stbir__2_coeff_only()  \
    float tota,totb,c;         \
    c = hc[0];                 \
    tota = decode[0]*c;        \
    totb = decode[1]*c;        \
    c = hc[1];                 \
    tota += decode[2]*c;       \
    totb += decode[3]*c;

// this weird order of add matches the simd
#define stbir__3_coeff_only()  \
    float tota,totb,c;         \
    c = hc[0];                 \
    tota = decode[0]*c;        \
    totb = decode[1]*c;        \
    c = hc[2];                 \
    tota += decode[4]*c;       \
    totb += decode[5]*c;       \
    c = hc[1];                 \
    tota += decode[2]*c;       \
    totb += decode[3]*c;

#define stbir__store_output_tiny()                \
    output[0] = tota;                             \
    output[1] = totb;                             \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 2;

#define stbir__4_coeff_start()      \
    float tota0,tota1,tota2,tota3,totb0,totb1,totb2,totb3,c;  \
    c = hc[0];                      \
    tota0 = decode[0]*c;            \
    totb0 = decode[1]*c;            \
    c = hc[1];                      \
    tota1 = decode[2]*c;            \
    totb1 = decode[3]*c;            \
    c = hc[2];                      \
    tota2 = decode[4]*c;            \
    totb2 = decode[5]*c;            \
    c = hc[3];                      \
    tota3 = decode[6]*c;            \
    totb3 = decode[7]*c;

#define stbir__4_coeff_continue_from_4( ofs )  \
    c = hc[0+(ofs)];                           \
    tota0 += decode[0+(ofs)*2]*c;              \
    totb0 += decode[1+(ofs)*2]*c;              \
    c = hc[1+(ofs)];                           \
    tota1 += decode[2+(ofs)*2]*c;              \
    totb1 += decode[3+(ofs)*2]*c;              \
    c = hc[2+(ofs)];                           \
    tota2 += decode[4+(ofs)*2]*c;              \
    totb2 += decode[5+(ofs)*2]*c;              \
    c = hc[3+(ofs)];                           \
    tota3 += decode[6+(ofs)*2]*c;              \
    totb3 += decode[7+(ofs)*2]*c;

#define stbir__1_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*2] * c;    \
    totb0 += decode[1+(ofs)*2] * c;

#define stbir__2_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*2] * c;    \
    totb0 += decode[1+(ofs)*2] * c;    \
    c = hc[1+(ofs)];                   \
    tota1 += decode[2+(ofs)*2] * c;    \
    totb1 += decode[3+(ofs)*2] * c;

#define stbir__3_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*2] * c;    \
    totb0 += decode[1+(ofs)*2] * c;    \
    c = hc[1+(ofs)];                   \
    tota1 += decode[2+(ofs)*2] * c;    \
    totb1 += decode[3+(ofs)*2] * c;    \
    c = hc[2+(ofs)];                   \
    tota2 += decode[4+(ofs)*2] * c;    \
    totb2 += decode[5+(ofs)*2] * c;

#define stbir__store_output()                     \
    output[0] = (tota0+tota2)+(tota1+tota3);      \
    output[1] = (totb0+totb2)+(totb1+totb3);      \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 2;

#define STBIR__horizontal_channels 2
#define STB_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCEFILE

//=================
// Do 3 channel horizontal routines

#define stbir__1_coeff_only()  \
    float tot0, tot1, tot2, c; \
    c = hc[0];                 \
    tot0 = decode[0]*c;        \
    tot1 = decode[1]*c;        \
    tot2 = decode[2]*c;

#define stbir__2_coeff_only()  \
    float tot0, tot1, tot2, c; \
    c = hc[0];                 \
    tot0 = decode[0]*c;        \
    tot1 = decode[1]*c;        \
    tot2 = decode[2]*c;        \
    c = hc[1];                 \
    tot0 += decode[3]*c;       \
    tot1 += decode[4]*c;       \
    tot2 += decode[5]*c;

#define stbir__3_coeff_only()  \
    float tot0, tot1, tot2, c; \
    c = hc[0];                 \
    tot0 = decode[0]*c;        \
    tot1 = decode[1]*c;        \
    tot2 = decode[2]*c;        \
    c = hc[1];                 \
    tot0 += decode[3]*c;       \
    tot1 += decode[4]*c;       \
    tot2 += decode[5]*c;       \
    c = hc[2];                 \
    tot0 += decode[6]*c;       \
    tot1 += decode[7]*c;       \
    tot2 += decode[8]*c;

#define stbir__store_output_tiny()                \
    output[0] = tot0;                             \
    output[1] = tot1;                             \
    output[2] = tot2;                             \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 3;

#define stbir__4_coeff_start()      \
    float tota0,tota1,tota2,totb0,totb1,totb2,totc0,totc1,totc2,totd0,totd1,totd2,c;  \
    c = hc[0];                      \
    tota0 = decode[0]*c;            \
    tota1 = decode[1]*c;            \
    tota2 = decode[2]*c;            \
    c = hc[1];                      \
    totb0 = decode[3]*c;            \
    totb1 = decode[4]*c;            \
    totb2 = decode[5]*c;            \
    c = hc[2];                      \
    totc0 = decode[6]*c;            \
    totc1 = decode[7]*c;            \
    totc2 = decode[8]*c;            \
    c = hc[3];                      \
    totd0 = decode[9]*c;            \
    totd1 = decode[10]*c;           \
    totd2 = decode[11]*c;

#define stbir__4_coeff_continue_from_4( ofs )  \
    c = hc[0+(ofs)];                           \
    tota0 += decode[0+(ofs)*3]*c;              \
    tota1 += decode[1+(ofs)*3]*c;              \
    tota2 += decode[2+(ofs)*3]*c;              \
    c = hc[1+(ofs)];                           \
    totb0 += decode[3+(ofs)*3]*c;              \
    totb1 += decode[4+(ofs)*3]*c;              \
    totb2 += decode[5+(ofs)*3]*c;              \
    c = hc[2+(ofs)];                           \
    totc0 += decode[6+(ofs)*3]*c;              \
    totc1 += decode[7+(ofs)*3]*c;              \
    totc2 += decode[8+(ofs)*3]*c;              \
    c = hc[3+(ofs)];                           \
    totd0 += decode[9+(ofs)*3]*c;              \
    totd1 += decode[10+(ofs)*3]*c;             \
    totd2 += decode[11+(ofs)*3]*c;

#define stbir__1_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*3]*c;      \
    tota1 += decode[1+(ofs)*3]*c;      \
    tota2 += decode[2+(ofs)*3]*c;

#define stbir__2_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*3]*c;      \
    tota1 += decode[1+(ofs)*3]*c;      \
    tota2 += decode[2+(ofs)*3]*c;      \
    c = hc[1+(ofs)];                   \
    totb0 += decode[3+(ofs)*3]*c;      \
    totb1 += decode[4+(ofs)*3]*c;      \
    totb2 += decode[5+(ofs)*3]*c;      \

#define stbir__3_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*3]*c;      \
    tota1 += decode[1+(ofs)*3]*c;      \
    tota2 += decode[2+(ofs)*3]*c;      \
    c = hc[1+(ofs)];                   \
    totb0 += decode[3+(ofs)*3]*c;      \
    totb1 += decode[4+(ofs)*3]*c;      \
    totb2 += decode[5+(ofs)*3]*c;      \
    c = hc[2+(ofs)];                   \
    totc0 += decode[6+(ofs)*3]*c;      \
    totc1 += decode[7+(ofs)*3]*c;      \
    totc2 += decode[8+(ofs)*3]*c;

#define stbir__store_output()                     \
    output[0] = (tota0+totc0)+(totb0+totd0);      \
    output[1] = (tota1+totc1)+(totb1+totd1);      \
    output[2] = (tota2+totc2)+(totb2+totd2);      \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 3;

#define STBIR__horizontal_channels 3
#define STB_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCEFILE

//=================
// Do 4 channel horizontal routines

#define stbir__1_coeff_only()         \
    float p0,p1,p2,p3,c;              \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0];                        \
    p0 = decode[0] * c;               \
    p1 = decode[1] * c;               \
    p2 = decode[2] * c;               \
    p3 = decode[3] * c;

#define stbir__2_coeff_only()         \
    float p0,p1,p2,p3,c;              \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0];                        \
    p0 = decode[0] * c;               \
    p1 = decode[1] * c;               \
    p2 = decode[2] * c;               \
    p3 = decode[3] * c;               \
    c = hc[1];                        \
    p0 += decode[4] * c;              \
    p1 += decode[5] * c;              \
    p2 += decode[6] * c;              \
    p3 += decode[7] * c;

#define stbir__3_coeff_only()         \
    float p0,p1,p2,p3,c;              \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0];                        \
    p0 = decode[0] * c;               \
    p1 = decode[1] * c;               \
    p2 = decode[2] * c;               \
    p3 = decode[3] * c;               \
    c = hc[1];                        \
    p0 += decode[4] * c;              \
    p1 += decode[5] * c;              \
    p2 += decode[6] * c;              \
    p3 += decode[7] * c;              \
    c = hc[2];                        \
    p0 += decode[8] * c;              \
    p1 += decode[9] * c;              \
    p2 += decode[10] * c;             \
    p3 += decode[11] * c;

#define stbir__store_output_tiny()                \
    output[0] = p0;                               \
    output[1] = p1;                               \
    output[2] = p2;                               \
    output[3] = p3;                               \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 4;

#define stbir__4_coeff_start()        \
    float x0,x1,x2,x3,y0,y1,y2,y3,c;  \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0];                        \
    x0 = decode[0] * c;               \
    x1 = decode[1] * c;               \
    x2 = decode[2] * c;               \
    x3 = decode[3] * c;               \
    c = hc[1];                        \
    y0 = decode[4] * c;               \
    y1 = decode[5] * c;               \
    y2 = decode[6] * c;               \
    y3 = decode[7] * c;               \
    c = hc[2];                        \
    x0 += decode[8] * c;              \
    x1 += decode[9] * c;              \
    x2 += decode[10] * c;             \
    x3 += decode[11] * c;             \
    c = hc[3];                        \
    y0 += decode[12] * c;             \
    y1 += decode[13] * c;             \
    y2 += decode[14] * c;             \
    y3 += decode[15] * c;

#define stbir__4_coeff_continue_from_4( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0+(ofs)];                  \
    x0 += decode[0+(ofs)*4] * c;      \
    x1 += decode[1+(ofs)*4] * c;      \
    x2 += decode[2+(ofs)*4] * c;      \
    x3 += decode[3+(ofs)*4] * c;      \
    c = hc[1+(ofs)];                  \
    y0 += decode[4+(ofs)*4] * c;      \
    y1 += decode[5+(ofs)*4] * c;      \
    y2 += decode[6+(ofs)*4] * c;      \
    y3 += decode[7+(ofs)*4] * c;      \
    c = hc[2+(ofs)];                  \
    x0 += decode[8+(ofs)*4] * c;      \
    x1 += decode[9+(ofs)*4] * c;      \
    x2 += decode[10+(ofs)*4] * c;     \
    x3 += decode[11+(ofs)*4] * c;     \
    c = hc[3+(ofs)];                  \
    y0 += decode[12+(ofs)*4] * c;     \
    y1 += decode[13+(ofs)*4] * c;     \
    y2 += decode[14+(ofs)*4] * c;     \
    y3 += decode[15+(ofs)*4] * c;

#define stbir__1_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0+(ofs)];                  \
    x0 += decode[0+(ofs)*4] * c;      \
    x1 += decode[1+(ofs)*4] * c;      \
    x2 += decode[2+(ofs)*4] * c;      \
    x3 += decode[3+(ofs)*4] * c;

#define stbir__2_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0+(ofs)];                  \
    x0 += decode[0+(ofs)*4] * c;      \
    x1 += decode[1+(ofs)*4] * c;      \
    x2 += decode[2+(ofs)*4] * c;      \
    x3 += decode[3+(ofs)*4] * c;      \
    c = hc[1+(ofs)];                  \
    y0 += decode[4+(ofs)*4] * c;      \
    y1 += decode[5+(ofs)*4] * c;      \
    y2 += decode[6+(ofs)*4] * c;      \
    y3 += decode[7+(ofs)*4] * c;

#define stbir__3_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0+(ofs)];                  \
    x0 += decode[0+(ofs)*4] * c;      \
    x1 += decode[1+(ofs)*4] * c;      \
    x2 += decode[2+(ofs)*4] * c;      \
    x3 += decode[3+(ofs)*4] * c;      \
    c = hc[1+(ofs)];                  \
    y0 += decode[4+(ofs)*4] * c;      \
    y1 += decode[5+(ofs)*4] * c;      \
    y2 += decode[6+(ofs)*4] * c;      \
    y3 += decode[7+(ofs)*4] * c;      \
    c = hc[2+(ofs)];                  \
    x0 += decode[8+(ofs)*4] * c;      \
    x1 += decode[9+(ofs)*4] * c;      \
    x2 += decode[10+(ofs)*4] * c;     \
    x3 += decode[11+(ofs)*4] * c;

#define stbir__store_output()                     \
    output[0] = x0 + y0;                          \
    output[1] = x1 + y1;                          \
    output[2] = x2 + y2;                          \
    output[3] = x3 + y3;                          \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 4;

#define STBIR__horizontal_channels 4
#define STB_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCEFILE

//=================
// Do 7 channel horizontal routines

#define stbir__1_coeff_only()        \
    float tot0, tot1, tot2, tot3, tot4, tot5, tot6, c; \
    c = hc[0];                       \
    tot0 = decode[0]*c;              \
    tot1 = decode[1]*c;              \
    tot2 = decode[2]*c;              \
    tot3 = decode[3]*c;              \
    tot4 = decode[4]*c;              \
    tot5 = decode[5]*c;              \
    tot6 = decode[6]*c;

#define stbir__2_coeff_only()        \
    float tot0, tot1, tot2, tot3, tot4, tot5, tot6, c; \
    c = hc[0];                       \
    tot0 = decode[0]*c;              \
    tot1 = decode[1]*c;              \
    tot2 = decode[2]*c;              \
    tot3 = decode[3]*c;              \
    tot4 = decode[4]*c;              \
    tot5 = decode[5]*c;              \
    tot6 = decode[6]*c;              \
    c = hc[1];                       \
    tot0 += decode[7]*c;             \
    tot1 += decode[8]*c;             \
    tot2 += decode[9]*c;             \
    tot3 += decode[10]*c;            \
    tot4 += decode[11]*c;            \
    tot5 += decode[12]*c;            \
    tot6 += decode[13]*c;            \

#define stbir__3_coeff_only()        \
    float tot0, tot1, tot2, tot3, tot4, tot5, tot6, c; \
    c = hc[0];                       \
    tot0 = decode[0]*c;              \
    tot1 = decode[1]*c;              \
    tot2 = decode[2]*c;              \
    tot3 = decode[3]*c;              \
    tot4 = decode[4]*c;              \
    tot5 = decode[5]*c;              \
    tot6 = decode[6]*c;              \
    c = hc[1];                       \
    tot0 += decode[7]*c;             \
    tot1 += decode[8]*c;             \
    tot2 += decode[9]*c;             \
    tot3 += decode[10]*c;            \
    tot4 += decode[11]*c;            \
    tot5 += decode[12]*c;            \
    tot6 += decode[13]*c;            \
    c = hc[2];                       \
    tot0 += decode[14]*c;            \
    tot1 += decode[15]*c;            \
    tot2 += decode[16]*c;            \
    tot3 += decode[17]*c;            \
    tot4 += decode[18]*c;            \
    tot5 += decode[19]*c;            \
    tot6 += decode[20]*c;            \

#define stbir__store_output_tiny()                \
    output[0] = tot0;                             \
    output[1] = tot1;                             \
    output[2] = tot2;                             \
    output[3] = tot3;                             \
    output[4] = tot4;                             \
    output[5] = tot5;                             \
    output[6] = tot6;                             \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 7;

#define stbir__4_coeff_start()    \
    float x0,x1,x2,x3,x4,x5,x6,y0,y1,y2,y3,y4,y5,y6,c; \
    STBIR_SIMD_NO_UNROLL(decode); \
    c = hc[0];                    \
    x0 = decode[0] * c;           \
    x1 = decode[1] * c;           \
    x2 = decode[2] * c;           \
    x3 = decode[3] * c;           \
    x4 = decode[4] * c;           \
    x5 = decode[5] * c;           \
    x6 = decode[6] * c;           \
    c = hc[1];                    \
    y0 = decode[7] * c;           \
    y1 = decode[8] * c;           \
    y2 = decode[9] * c;           \
    y3 = decode[10] * c;          \
    y4 = decode[11] * c;          \
    y5 = decode[12] * c;          \
    y6 = decode[13] * c;          \
    c = hc[2];                    \
    x0 += decode[14] * c;         \
    x1 += decode[15] * c;         \
    x2 += decode[16] * c;         \
    x3 += decode[17] * c;         \
    x4 += decode[18] * c;         \
    x5 += decode[19] * c;         \
    x6 += decode[20] * c;         \
    c = hc[3];                    \
    y0 += decode[21] * c;         \
    y1 += decode[22] * c;         \
    y2 += decode[23] * c;         \
    y3 += decode[24] * c;         \
    y4 += decode[25] * c;         \
    y5 += decode[26] * c;         \
    y6 += decode[27] * c;

#define stbir__4_coeff_continue_from_4( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);  \
    c = hc[0+(ofs)];               \
    x0 += decode[0+(ofs)*7] * c;   \
    x1 += decode[1+(ofs)*7] * c;   \
    x2 += decode[2+(ofs)*7] * c;   \
    x3 += decode[3+(ofs)*7] * c;   \
    x4 += decode[4+(ofs)*7] * c;   \
    x5 += decode[5+(ofs)*7] * c;   \
    x6 += decode[6+(ofs)*7] * c;   \
    c = hc[1+(ofs)];               \
    y0 += decode[7+(ofs)*7] * c;   \
    y1 += decode[8+(ofs)*7] * c;   \
    y2 += decode[9+(ofs)*7] * c;   \
    y3 += decode[10+(ofs)*7] * c;  \
    y4 += decode[11+(ofs)*7] * c;  \
    y5 += decode[12+(ofs)*7] * c;  \
    y6 += decode[13+(ofs)*7] * c;  \
    c = hc[2+(ofs)];               \
    x0 += decode[14+(ofs)*7] * c;  \
    x1 += decode[15+(ofs)*7] * c;  \
    x2 += decode[16+(ofs)*7] * c;  \
    x3 += decode[17+(ofs)*7] * c;  \
    x4 += decode[18+(ofs)*7] * c;  \
    x5 += decode[19+(ofs)*7] * c;  \
    x6 += decode[20+(ofs)*7] * c;  \
    c = hc[3+(ofs)];               \
    y0 += decode[21+(ofs)*7] * c;  \
    y1 += decode[22+(ofs)*7] * c;  \
    y2 += decode[23+(ofs)*7] * c;  \
    y3 += decode[24+(ofs)*7] * c;  \
    y4 += decode[25+(ofs)*7] * c;  \
    y5 += decode[26+(ofs)*7] * c;  \
    y6 += decode[27+(ofs)*7] * c;

#define stbir__1_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);  \
    c = hc[0+(ofs)];               \
    x0 += decode[0+(ofs)*7] * c;   \
    x1 += decode[1+(ofs)*7] * c;   \
    x2 += decode[2+(ofs)*7] * c;   \
    x3 += decode[3+(ofs)*7] * c;   \
    x4 += decode[4+(ofs)*7] * c;   \
    x5 += decode[5+(ofs)*7] * c;   \
    x6 += decode[6+(ofs)*7] * c;   \

#define stbir__2_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);  \
    c = hc[0+(ofs)];               \
    x0 += decode[0+(ofs)*7] * c;   \
    x1 += decode[1+(ofs)*7] * c;   \
    x2 += decode[2+(ofs)*7] * c;   \
    x3 += decode[3+(ofs)*7] * c;   \
    x4 += decode[4+(ofs)*7] * c;   \
    x5 += decode[5+(ofs)*7] * c;   \
    x6 += decode[6+(ofs)*7] * c;   \
    c = hc[1+(ofs)];               \
    y0 += decode[7+(ofs)*7] * c;   \
    y1 += decode[8+(ofs)*7] * c;   \
    y2 += decode[9+(ofs)*7] * c;   \
    y3 += decode[10+(ofs)*7] * c;  \
    y4 += decode[11+(ofs)*7] * c;  \
    y5 += decode[12+(ofs)*7] * c;  \
    y6 += decode[13+(ofs)*7] * c;  \

#define stbir__3_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);  \
    c = hc[0+(ofs)];               \
    x0 += decode[0+(ofs)*7] * c;   \
    x1 += decode[1+(ofs)*7] * c;   \
    x2 += decode[2+(ofs)*7] * c;   \
    x3 += decode[3+(ofs)*7] * c;   \
    x4 += decode[4+(ofs)*7] * c;   \
    x5 += decode[5+(ofs)*7] * c;   \
    x6 += decode[6+(ofs)*7] * c;   \
    c = hc[1+(ofs)];               \
    y0 += decode[7+(ofs)*7] * c;   \
    y1 += decode[8+(ofs)*7] * c;   \
    y2 += decode[9+(ofs)*7] * c;   \
    y3 += decode[10+(ofs)*7] * c;  \
    y4 += decode[11+(ofs)*7] * c;  \
    y5 += decode[12+(ofs)*7] * c;  \
    y6 += decode[13+(ofs)*7] * c;  \
    c = hc[2+(ofs)];               \
    x0 += decode[14+(ofs)*7] * c;  \
    x1 += decode[15+(ofs)*7] * c;  \
    x2 += decode[16+(ofs)*7] * c;  \
    x3 += decode[17+(ofs)*7] * c;  \
    x4 += decode[18+(ofs)*7] * c;  \
    x5 += decode[19+(ofs)*7] * c;  \
    x6 += decode[20+(ofs)*7] * c;  \

#define stbir__store_output()                     \
    output[0] = x0 + y0;                          \
    output[1] = x1 + y1;                          \
    output[2] = x2 + y2;                          \
    output[3] = x3 + y3;                          \
    output[4] = x4 + y4;                          \
    output[5] = x5 + y5;                          \
    output[6] = x6 + y6;                          \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 7;

#define STBIR__horizontal_channels 7
#define STB_IMAGE_RESIZE_DO_HORIZONTALS
#include SOURCEFILE

// include all of the vertical resamplers (both scatter and gather versions)

#define STBIR__vertical_channels 1
#define STB_IMAGE_RESIZE_DO_VERTICALS
#include SOURCEFILE

#define STBIR__vertical_channels 1
#define STB_IMAGE_RESIZE_DO_VERTICALS
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCEFILE

#define STBIR__vertical_channels 2
#define STB_IMAGE_RESIZE_DO_VERTICALS
#include SOURCEFILE

#define STBIR__vertical_channels 2
#define STB_IMAGE_RESIZE_DO_VERTICALS
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCEFILE

#define STBIR__vertical_channels 3
#define STB_IMAGE_RESIZE_DO_VERTICALS
#include SOURCEFILE

#define STBIR__vertical_channels 3
#define STB_IMAGE_RESIZE_DO_VERTICALS
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCEFILE

#define STBIR__vertical_channels 4
#define STB_IMAGE_RESIZE_DO_VERTICALS
#include SOURCEFILE

#define STBIR__vertical_channels 4
#define STB_IMAGE_RESIZE_DO_VERTICALS
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCEFILE

#define STBIR__vertical_channels 5
#define STB_IMAGE_RESIZE_DO_VERTICALS
#include SOURCEFILE

#define STBIR__vertical_channels 5
#define STB_IMAGE_RESIZE_DO_VERTICALS
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCEFILE

#define STBIR__vertical_channels 6
#define STB_IMAGE_RESIZE_DO_VERTICALS
#include SOURCEFILE

#define STBIR__vertical_channels 6
#define STB_IMAGE_RESIZE_DO_VERTICALS
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCEFILE

#define STBIR__vertical_channels 7
#define STB_IMAGE_RESIZE_DO_VERTICALS
#include SOURCEFILE

#define STBIR__vertical_channels 7
#define STB_IMAGE_RESIZE_DO_VERTICALS
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCEFILE

#define STBIR__vertical_channels 8
#define STB_IMAGE_RESIZE_DO_VERTICALS
#include SOURCEFILE

#define STBIR__vertical_channels 8
#define STB_IMAGE_RESIZE_DO_VERTICALS
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include SOURCEFILE

static stbir__horizontal_gather_channels_func ** stbir__horizontal_gather_n_coeffs_funcs[8] =
{
  0, stbir__horizontal_gather_1_channels_with_n_coeffs_funcs, stbir__horizontal_gather_2_channels_with_n_coeffs_funcs, stbir__horizontal_gather_3_channels_with_n_coeffs_funcs, stbir__horizontal_gather_4_channels_with_n_coeffs_funcs, 0,0, stbir__horizontal_gather_7_channels_with_n_coeffs_funcs
};

static stbir__horizontal_gather_channels_func ** stbir__horizontal_gather_channels_funcs[8] =
{
  0, stbir__horizontal_gather_1_channels_funcs, stbir__horizontal_gather_2_channels_funcs, stbir__horizontal_gather_3_channels_funcs, stbir__horizontal_gather_4_channels_funcs, 0,0, stbir__horizontal_gather_7_channels_funcs
};

static int stbir__edge_wrap(int n, int max)
{
  // avoid per-pixel switch
  if (n >= 0 && n < max)
    return n;
  return stbir__edge_clamp_full( n, max );
}

// get information on the extents of a sampler
static void stbir__get_extents( stbir__sampler * samp, stbir__extents * scanline_extents )
{
  int j, stop;
  int left_margin, right_margin;
  int min_n = 0x7fffffff, max_n = -0x7fffffff;
  int min_left = 0x7fffffff, max_left = -0x7fffffff;
  int min_right = 0x7fffffff, max_right = -0x7fffffff;
  stbir__contributors* contributors = samp->contributors;
  int output_sub_size = samp->scale_info.output_sub_size;
  int input_full_size = samp->scale_info.input_full_size;
  int filter_pixel_margin = samp->filter_pixel_margin;

  assert( samp->is_gather );

  stop = output_sub_size;
  for (j = 0; j < stop; j++ )
  {
    assert( contributors[j].n1 >= contributors[j].n0 );
    if ( contributors[j].n0 < min_n )
    {
      min_n = contributors[j].n0;
      stop = j + filter_pixel_margin;  // if we find a new min, only scan another filter width
      if ( stop > output_sub_size ) stop = output_sub_size;
    }
  }

  stop = 0;
  for (j = output_sub_size - 1; j >= stop; j-- )
  {
    assert( contributors[j].n1 >= contributors[j].n0 );
    if ( contributors[j].n1 > max_n )
    {
      max_n = contributors[j].n1;
      stop = j - filter_pixel_margin;  // if we find a new max, only scan another filter width
      if (stop<0) stop = 0;
    }
  }

  assert( scanline_extents->conservative.n0 <= min_n );
  assert( scanline_extents->conservative.n1 >= max_n );

  // now calculate how much into the margins we really read
  left_margin = 0;
  if ( min_n < 0 )
  {
    left_margin = -min_n;
    min_n = 0;
  }

  right_margin = 0;
  if ( max_n >= input_full_size )
  {
    right_margin = max_n - input_full_size + 1;
    max_n = input_full_size - 1;
  }

  // index 1 is margin pixel extents (how many pixels we hang over the edge)
  scanline_extents->edge_sizes[0] = left_margin;
  scanline_extents->edge_sizes[1] = right_margin;

  // index 2 is pixels read from the input
  scanline_extents->spans[0].n0 = min_n;
  scanline_extents->spans[0].n1 = max_n;
  scanline_extents->spans[0].pixel_offset_for_input = min_n;

  // default to no other input range
  scanline_extents->spans[1].n0 = 0;
  scanline_extents->spans[1].n1 = -1;
  scanline_extents->spans[1].pixel_offset_for_input = 0;

  // convert margin pixels to the pixels within the input (min and max)
  for( j = -left_margin ; j < 0 ; j++ )
  {
      int p = stbir__edge_wrap( j, input_full_size );
      if ( p < min_left )
        min_left = p;
      if ( p > max_left )
        max_left = p;
  }

  for( j = input_full_size ; j < (input_full_size + right_margin) ; j++ )
  {
      int p = stbir__edge_wrap( j, input_full_size );
      if ( p < min_right )
        min_right = p;
      if ( p > max_right )
        max_right = p;
  }

  // merge the left margin pixel region if it connects within 4 pixels of main pixel region
  if ( min_left != 0x7fffffff )
  {
    if ( ( ( min_left <= min_n ) && ( ( max_left  + STBIR__MERGE_RUNS_PIXEL_THRESHOLD ) >= min_n ) ) ||
         ( ( min_n <= min_left ) && ( ( max_n  + STBIR__MERGE_RUNS_PIXEL_THRESHOLD ) >= max_left ) ) )
    {
      scanline_extents->spans[0].n0 = min_n = stbir__min( min_n, min_left );
      scanline_extents->spans[0].n1 = max_n = stbir__max( max_n, max_left );
      scanline_extents->spans[0].pixel_offset_for_input = min_n;
      left_margin = 0;
    }
  }

  // merge the right margin pixel region if it connects within 4 pixels of main pixel region
  if ( min_right != 0x7fffffff )
  {
    if ( ( ( min_right <= min_n ) && ( ( max_right  + STBIR__MERGE_RUNS_PIXEL_THRESHOLD ) >= min_n ) ) ||
         ( ( min_n <= min_right ) && ( ( max_n  + STBIR__MERGE_RUNS_PIXEL_THRESHOLD ) >= max_right ) ) )
    {
      scanline_extents->spans[0].n0 = min_n = stbir__min( min_n, min_right );
      scanline_extents->spans[0].n1 = max_n = stbir__max( max_n, max_right );
      scanline_extents->spans[0].pixel_offset_for_input = min_n;
      right_margin = 0;
    }
  }

  assert( scanline_extents->conservative.n0 <= min_n );
  assert( scanline_extents->conservative.n1 >= max_n );

  // you get two ranges when you have the WRAP edge mode and you are doing just the a piece of the resize
  //   so you need to get a second run of pixels from the opposite side of the scanline (which you
  //   wouldn't need except for WRAP)


  // if we can't merge the min_left range, add it as a second range
  if ( ( left_margin ) && ( min_left != 0x7fffffff ) )
  {
    stbir__span * newspan = scanline_extents->spans + 1;
    assert( right_margin == 0 );
    if ( min_left < scanline_extents->spans[0].n0 )
    {
      scanline_extents->spans[1].pixel_offset_for_input = scanline_extents->spans[0].n0;
      scanline_extents->spans[1].n0 = scanline_extents->spans[0].n0;
      scanline_extents->spans[1].n1 = scanline_extents->spans[0].n1;
      --newspan;
    }
    newspan->pixel_offset_for_input = min_left;
    newspan->n0 = -left_margin;
    newspan->n1 = ( max_left - min_left ) - left_margin;
    scanline_extents->edge_sizes[0] = 0;  // don't need to copy the left margin, since we are directly decoding into the margin
  }
  // if we can't merge the min_left range, add it as a second range
  else
  if ( ( right_margin ) && ( min_right != 0x7fffffff ) )
  {
    stbir__span * newspan = scanline_extents->spans + 1;
    if ( min_right < scanline_extents->spans[0].n0 )
    {
      scanline_extents->spans[1].pixel_offset_for_input = scanline_extents->spans[0].n0;
      scanline_extents->spans[1].n0 = scanline_extents->spans[0].n0;
      scanline_extents->spans[1].n1 = scanline_extents->spans[0].n1;
      --newspan;
    }
    newspan->pixel_offset_for_input = min_right;
    newspan->n0 = scanline_extents->spans[1].n1 + 1;
    newspan->n1 = scanline_extents->spans[1].n1 + 1 + ( max_right - min_right );
    scanline_extents->edge_sizes[1] = 0;  // don't need to copy the right margin, since we are directly decoding into the margin
  }

  // sort the spans into write output order
  if ( ( scanline_extents->spans[1].n1 > scanline_extents->spans[1].n0 ) && ( scanline_extents->spans[0].n0 > scanline_extents->spans[1].n0 ) )
  {
    stbir__span tspan = scanline_extents->spans[0];
    scanline_extents->spans[0] = scanline_extents->spans[1];
    scanline_extents->spans[1] = tspan;
  }
}

static int stbir__pack_coefficients( int num_contributors, stbir__contributors* contributors, float * coefficents, int coefficient_width, int widest, int row0, int row1 )
{
  #define STBIR_MOVE_1( dest, src ) { STBIR_NO_UNROLL(dest); ((uint32_t*)(dest))[0] = ((uint32_t*)(src))[0]; }
  #define STBIR_MOVE_2( dest, src ) { STBIR_NO_UNROLL(dest); ((uint64_t*)(dest))[0] = ((uint64_t*)(src))[0]; }
  #define STBIR_MOVE_4( dest, src ) { STBIR_NO_UNROLL(dest); ((uint64_t*)(dest))[0] = ((uint64_t*)(src))[0]; ((uint64_t*)(dest))[1] = ((uint64_t*)(src))[1]; }
  int row_end = row1 + 1;// only used in an assert

  if ( coefficient_width != widest )
  {
    float * pc = coefficents;
    float * coeffs = coefficents;
    float * pc_end = coefficents + num_contributors * widest;
    switch( widest )
    {
      case 1:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_1( pc, coeffs );
          ++pc;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 2:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_2( pc, coeffs );
          pc += 2;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 3:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_2( pc, coeffs );
          STBIR_MOVE_1( pc+2, coeffs+2 );
          pc += 3;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 4:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          pc += 4;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 5:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_1( pc+4, coeffs+4 );
          pc += 5;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 6:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_2( pc+4, coeffs+4 );
          pc += 6;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 7:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_2( pc+4, coeffs+4 );
          STBIR_MOVE_1( pc+6, coeffs+6 );
          pc += 7;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 8:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          pc += 8;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 9:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          STBIR_MOVE_1( pc+8, coeffs+8 );
          pc += 9;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 10:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          STBIR_MOVE_2( pc+8, coeffs+8 );
          pc += 10;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 11:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          STBIR_MOVE_2( pc+8, coeffs+8 );
          STBIR_MOVE_1( pc+10, coeffs+10 );
          pc += 11;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 12:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          STBIR_MOVE_4( pc+8, coeffs+8 );
          pc += 12;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      default:
        STBIR_NO_UNROLL_LOOP_START
        do {
          float * copy_end = pc + widest - 4;
          float * c = coeffs;
          do {
            STBIR_NO_UNROLL( pc );
            STBIR_MOVE_4( pc, c );
            pc += 4;
            c += 4;
          } while ( pc <= copy_end );
          copy_end += 4;
          STBIR_NO_UNROLL_LOOP_START
          while ( pc < copy_end )
          {
            STBIR_MOVE_1( pc, c );
            ++pc; ++c;
          }
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
    }
  }

  // some horizontal routines read one float off the end (which is then masked off), so put in a sentinal so we don't read an snan or denormal
  coefficents[ widest * num_contributors ] = 8888.0f;

  // the minimum we might read for unrolled filters widths is 12. So, we need to
  //   make sure we never read outside the decode buffer, by possibly moving
  //   the sample area back into the scanline, and putting zeros weights first.
  // we start on the right edge and check until we're well past the possible
  //   clip area (2*widest).
  {
    stbir__contributors * contribs = contributors + num_contributors - 1;
    float * coeffs = coefficents + widest * ( num_contributors - 1 );

    // go until no chance of clipping (this is usually less than 8 lops)
    while ( ( contribs >= contributors ) && ( ( contribs->n0 + widest*2 ) >= row_end ) )
    {
      // might we clip??
      if ( ( contribs->n0 + widest ) > row_end )
      {
        int stop_range = widest;

        // if range is larger than 12, it will be handled by generic loops that can terminate on the exact length
        //   of this contrib n1, instead of a fixed widest amount - so calculate this
        if ( widest > 12 )
        {
          int mod;

          // how far will be read in the n_coeff loop (which depends on the widest count mod4);
          mod = widest & 3;
          stop_range = ( ( ( contribs->n1 - contribs->n0 + 1 ) - mod + 3 ) & ~3 ) + mod;

          // the n_coeff loops do a minimum amount of coeffs, so factor that in!
          if ( stop_range < ( 8 + mod ) ) stop_range = 8 + mod;
        }

        // now see if we still clip with the refined range
        if ( ( contribs->n0 + stop_range ) > row_end )
        {
          int new_n0 = row_end - stop_range;
          int num = contribs->n1 - contribs->n0 + 1;
          int backup = contribs->n0 - new_n0;
          float * from_co = coeffs + num - 1;
          float * to_co = from_co + backup;

          assert( ( new_n0 >= row0 ) && ( new_n0 < contribs->n0 ) );

          // move the coeffs over
          while( num )
          {
            *to_co-- = *from_co--;
            --num;
          }
          // zero new positions
          while ( to_co >= coeffs )
            *to_co-- = 0;
          // set new start point
          contribs->n0 = new_n0;
          if ( widest > 12 )
          {
            int mod;

            // how far will be read in the n_coeff loop (which depends on the widest count mod4);
            mod = widest & 3;
            stop_range = ( ( ( contribs->n1 - contribs->n0 + 1 ) - mod + 3 ) & ~3 ) + mod;

            // the n_coeff loops do a minimum amount of coeffs, so factor that in!
            if ( stop_range < ( 8 + mod ) ) stop_range = 8 + mod;
          }
        }
      }
      --contribs;
      coeffs -= widest;
    }
  }

  return widest;
  #undef STBIR_MOVE_1
  #undef STBIR_MOVE_2
  #undef STBIR_MOVE_4
}

static void stbir__get_split_info( stbir__per_split_info* split_info, int splits, int output_height, int vertical_pixel_margin, int input_full_height )
{
  int i, cur;
  int left = output_height;

  cur = 0;
  for( i = 0 ; i < splits ; i++ )
  {
    int each;
    split_info[i].start_output_y = cur;
    each = left / ( splits - i );
    split_info[i].end_output_y = cur + each;
    cur += each;
    left -= each;

    // scatter range (updated to minimum as you run it)
    split_info[i].start_input_y = -vertical_pixel_margin;
    split_info[i].end_input_y = input_full_height + vertical_pixel_margin;
  }
}

static ResizeInfo * stbir__alloc_internal_mem_and_build_samplers( stbir__sampler * horizontal, stbir__sampler * vertical, stbir__contributors * conservative, PixelLayout input_pixel_layout_public, PixelLayout output_pixel_layout_public, int splits, int new_x, int new_y)
{
  static char stbir_channel_count_index[8]={ 9,0,1,2, 3,9,9,4 };

  ResizeInfo * info = 0;
  void * alloced = 0;
  size_t alloced_total = 0;
  int vertical_first;
  size_t decode_buffer_size, ring_buffer_length_bytes, ring_buffer_size, vertical_buffer_size;
  int alloc_ring_buffer_num_entries;

  int alpha_weighting_type = 0; // 0=none, 1=simple, 2=fancy
  int conservative_split_output_size = stbir__get_max_split( splits, vertical->scale_info.output_sub_size );
  stbir_internal_pixel_layout input_pixel_layout = stbir__pixel_layout_convert_public_to_internal[ static_cast<size_t>(input_pixel_layout_public) ];
  stbir_internal_pixel_layout output_pixel_layout = stbir__pixel_layout_convert_public_to_internal[ static_cast<size_t>(output_pixel_layout_public) ];
  int channels = stbir__pixel_channels[ input_pixel_layout ];
  int effective_channels = channels;

  if ( ( input_pixel_layout >= STBIRI_RGBA ) && ( input_pixel_layout <= STBIRI_AR ) && ( output_pixel_layout >= STBIRI_RGBA ) && ( output_pixel_layout <= STBIRI_AR ) )
  {
      static int fancy_alpha_effective_cnts[6] = { 7, 7, 7, 7, 3, 3 };
      alpha_weighting_type = 2;
      effective_channels = fancy_alpha_effective_cnts[ input_pixel_layout - STBIRI_RGBA ];
  }
  else if ( ( input_pixel_layout >= STBIRI_RGBA_PM ) && ( input_pixel_layout <= STBIRI_AR_PM ) && ( output_pixel_layout >= STBIRI_RGBA ) && ( output_pixel_layout <= STBIRI_AR ) )
  {
    // input premult, output non-premult
    alpha_weighting_type = 3;
  }
  else if ( ( input_pixel_layout >= STBIRI_RGBA ) && ( input_pixel_layout <= STBIRI_AR ) && ( output_pixel_layout >= STBIRI_RGBA_PM ) && ( output_pixel_layout <= STBIRI_AR_PM ) )
  {
    // input non-premult, output premult
    alpha_weighting_type = 1;
  }

  // channel in and out count must match currently
  if ( channels != stbir__pixel_channels[ output_pixel_layout ] )
    return 0;

  // get vertical first
  vertical_first = stbir__should_do_vertical_first( stbir__compute_weights[ (int)stbir_channel_count_index[ effective_channels ] ], horizontal->filter_pixel_width, horizontal->scale_info.scale, horizontal->scale_info.output_sub_size, vertical->filter_pixel_width, vertical->scale_info.scale, vertical->scale_info.output_sub_size, vertical->is_gather);

  // sometimes read one float off in some of the unrolled loops (with a weight of zero coeff, so it doesn't have an effect)
  //   we use a few extra floats instead of just 1, so that input callback buffer can overlap with the decode buffer without
  //   the conversion routines overwriting the callback input data.
  decode_buffer_size = (unsigned long)(( conservative->n1 - conservative->n0 + 1 ) * effective_channels) * sizeof(float) + sizeof(float) * STBIR_INPUT_CALLBACK_PADDING; // extra floats for input callback stagger

  ring_buffer_length_bytes = (size_t)horizontal->scale_info.output_sub_size * (size_t)effective_channels * sizeof(float) + sizeof(float)*STBIR_INPUT_CALLBACK_PADDING; // extra floats for padding

  // if we do vertical first, the ring buffer holds a whole decoded line
  if ( vertical_first ) {
    ring_buffer_length_bytes = ( decode_buffer_size + 15 ) & (size_t)~15;
  }

  if ( ( ring_buffer_length_bytes & 4095 ) == 0 ) ring_buffer_length_bytes += 64*3; // avoid 4k alias

  // One extra entry because floating point precision problems sometimes cause an extra to be necessary.
  alloc_ring_buffer_num_entries = vertical->filter_pixel_width + 1;

  // we never need more ring buffer entries than the scanlines we're outputting when in scatter mode
  if ( ( !vertical->is_gather ) && ( alloc_ring_buffer_num_entries > conservative_split_output_size ) )
    alloc_ring_buffer_num_entries = conservative_split_output_size;

  ring_buffer_size = (size_t)alloc_ring_buffer_num_entries * (size_t)ring_buffer_length_bytes;

  // The vertical buffer is used differently, depending on whether we are scattering
  //   the vertical scanlines, or gathering them.
  //   If scattering, it's used at the temp buffer to accumulate each output.
  //   If gathering, it's just the output buffer.
  vertical_buffer_size = (size_t)horizontal->scale_info.output_sub_size * (size_t)effective_channels * sizeof(float) + sizeof(float);  // extra float for padding

  // we make two passes through this loop, 1st to add everything up, 2nd to allocate and init
  for(;;)
  {
    int i;
    void * advance_mem = alloced;
    int copy_horizontal = 0;
    stbir__sampler * possibly_use_horizontal_for_pivot = 0;

    #define STBIR__NEXT_PTR( ptr, size, ntype ) advance_mem = (void*) ( ( ((size_t)advance_mem) + 15 ) & (size_t)~15 ); if ( alloced ) ptr = (ntype*)advance_mem; advance_mem = ((char*)advance_mem) + (size);

    STBIR__NEXT_PTR( info, sizeof( ResizeInfo ), ResizeInfo );

    STBIR__NEXT_PTR( info->split_info, sizeof( stbir__per_split_info ) * (unsigned long)splits, stbir__per_split_info );

    if ( info )
    {
      static stbir__alpha_weight_func * fancy_alpha_weights[6]  =    { stbir__fancy_alpha_weight_4ch,   stbir__fancy_alpha_weight_4ch,   stbir__fancy_alpha_weight_4ch,   stbir__fancy_alpha_weight_4ch,   stbir__fancy_alpha_weight_2ch,   stbir__fancy_alpha_weight_2ch };
      static stbir__alpha_unweight_func * fancy_alpha_unweights[6] = { stbir__fancy_alpha_unweight_4ch, stbir__fancy_alpha_unweight_4ch, stbir__fancy_alpha_unweight_4ch, stbir__fancy_alpha_unweight_4ch, stbir__fancy_alpha_unweight_2ch, stbir__fancy_alpha_unweight_2ch };
      static stbir__alpha_weight_func * simple_alpha_weights[6] = { stbir__simple_alpha_weight_4ch, stbir__simple_alpha_weight_4ch, stbir__simple_alpha_weight_4ch, stbir__simple_alpha_weight_4ch, stbir__simple_alpha_weight_2ch, stbir__simple_alpha_weight_2ch };
      static stbir__alpha_unweight_func * simple_alpha_unweights[6] = { stbir__simple_alpha_unweight_4ch, stbir__simple_alpha_unweight_4ch, stbir__simple_alpha_unweight_4ch, stbir__simple_alpha_unweight_4ch, stbir__simple_alpha_unweight_2ch, stbir__simple_alpha_unweight_2ch };

      // initialize info fields
      info->alloced_mem = alloced;
      info->alloced_total = alloced_total;

      info->channels = channels;
      info->effective_channels = effective_channels;

      info->offset_x = new_x;
      info->offset_y = new_y;
      info->alloc_ring_buffer_num_entries = (int)alloc_ring_buffer_num_entries;
      info->ring_buffer_num_entries = 0;
      info->ring_buffer_length_bytes = (int)ring_buffer_length_bytes;
      info->splits = splits;
      info->vertical_first = vertical_first;

      info->input_pixel_layout_internal = input_pixel_layout;
      info->output_pixel_layout_internal = output_pixel_layout;

      // setup alpha weight functions
      info->alpha_weight = 0;
      info->alpha_unweight = 0;

      // handle alpha weighting functions and overrides
      if ( alpha_weighting_type == 2 )
      {
        // high quality alpha multiplying on the way in, dividing on the way out
        info->alpha_weight = fancy_alpha_weights[ input_pixel_layout - STBIRI_RGBA ];
        info->alpha_unweight = fancy_alpha_unweights[ output_pixel_layout - STBIRI_RGBA ];
      }
      else if ( alpha_weighting_type == 1 )
      {
        // fast alpha on the way in, leave in premultiplied form on way out
        info->alpha_weight = simple_alpha_weights[ input_pixel_layout - STBIRI_RGBA ];
      }
      else if ( alpha_weighting_type == 3 )
      {
        // incoming is premultiplied, fast alpha dividing on the way out - non-premultiplied output
        info->alpha_unweight = simple_alpha_unweights[ output_pixel_layout - STBIRI_RGBA ];
      }

      // handle 3-chan color flipping, using the alpha weight path
      if ( ( ( input_pixel_layout == STBIRI_RGB ) && ( output_pixel_layout == STBIRI_BGR ) ) ||
           ( ( input_pixel_layout == STBIRI_BGR ) && ( output_pixel_layout == STBIRI_RGB ) ) )
      {
        // do the flipping on the smaller of the two ends
        if ( horizontal->scale_info.scale < 1.0f )
          info->alpha_unweight = stbir__simple_flip_3ch;
        else
          info->alpha_weight = stbir__simple_flip_3ch;
      }

    }

    // get all the per-split buffers
    for( i = 0 ; i < splits ; i++ )
    {
      STBIR__NEXT_PTR( info->split_info[i].decode_buffer, decode_buffer_size, float );
      STBIR__NEXT_PTR( info->split_info[i].ring_buffer, ring_buffer_size, float );
      STBIR__NEXT_PTR( info->split_info[i].vertical_buffer, vertical_buffer_size, float );
    }

    // alloc memory for to-be-pivoted coeffs (if necessary)
    if ( vertical->is_gather == 0 )
    {
      size_t both;
      size_t temp_mem_amt;

      // when in vertical scatter mode, we first build the coefficients in gather mode, and then pivot after,
      //   that means we need two buffers, so we try to use the decode buffer and ring buffer for this. if that
      //   is too small, we just allocate extra memory to use as this temp.

      both = (size_t)vertical->gather_prescatter_contributors_size + (size_t)vertical->gather_prescatter_coefficients_size;
      temp_mem_amt = (size_t)( decode_buffer_size + ring_buffer_size + vertical_buffer_size ) * (size_t)splits;
      if ( temp_mem_amt >= both )
      {
        if ( info )
        {
          vertical->gather_prescatter_contributors = (stbir__contributors*)info->split_info[0].decode_buffer;
          vertical->gather_prescatter_coefficients = (float*) ( ( (char*)info->split_info[0].decode_buffer ) + vertical->gather_prescatter_contributors_size );
        }
      }
      else
      {
        // ring+decode memory is too small, so allocate temp memory
        STBIR__NEXT_PTR( vertical->gather_prescatter_contributors, vertical->gather_prescatter_contributors_size, stbir__contributors );
        STBIR__NEXT_PTR( vertical->gather_prescatter_coefficients, vertical->gather_prescatter_coefficients_size, float );
      }
    }

    STBIR__NEXT_PTR( horizontal->contributors, horizontal->contributors_size, stbir__contributors );
    STBIR__NEXT_PTR( horizontal->coefficients, horizontal->coefficients_size, float );

    // are the two filters identical?? (happens a lot with mipmap generation)
    if (horizontal->scale_info.output_sub_size == vertical->scale_info.output_sub_size)
    {
      float diff_scale = horizontal->scale_info.scale - vertical->scale_info.scale;
      float diff_shift = horizontal->scale_info.pixel_shift - vertical->scale_info.pixel_shift;
      if ( diff_scale < 0.0f ) diff_scale = -diff_scale;
      if ( diff_shift < 0.0f ) diff_shift = -diff_shift;
      if ( ( diff_scale <= stbir__small_float ) && ( diff_shift <= stbir__small_float ) )
      {
        if ( horizontal->is_gather == vertical->is_gather )
        {
          copy_horizontal = 1;
          goto no_vert_alloc;
        }
        // everything matches, but vertical is scatter, horizontal is gather, use horizontal coeffs for vertical pivot coeffs
        possibly_use_horizontal_for_pivot = horizontal;
      }
    }

    STBIR__NEXT_PTR( vertical->contributors, vertical->contributors_size, stbir__contributors );
    STBIR__NEXT_PTR( vertical->coefficients, vertical->coefficients_size, float );

   no_vert_alloc:

    if ( info )
    {

      stbir__calculate_filters( horizontal, 0);

      // setup the horizontal gather functions
      // start with defaulting to the n_coeffs functions (specialized on channels and remnant leftover)
      info->horizontal_gather_channels = stbir__horizontal_gather_n_coeffs_funcs[ effective_channels ][ horizontal->extent_info.widest & 3 ];
      // but if the number of coeffs <= 12, use another set of special cases. <=12 coeffs is any enlarging resize, or shrinking resize down to about 1/3 size
      if ( horizontal->extent_info.widest <= 12 )
        info->horizontal_gather_channels = stbir__horizontal_gather_channels_funcs[ effective_channels ][ horizontal->extent_info.widest - 1 ];

      info->scanline_extents.conservative.n0 = conservative->n0;
      info->scanline_extents.conservative.n1 = conservative->n1;

      // get exact extents
      stbir__get_extents( horizontal, &info->scanline_extents );

      // pack the horizontal coeffs
      horizontal->coefficient_width = stbir__pack_coefficients(horizontal->num_contributors, horizontal->contributors, horizontal->coefficients, horizontal->coefficient_width, horizontal->extent_info.widest, info->scanline_extents.conservative.n0, info->scanline_extents.conservative.n1 );

      memcpy( &info->horizontal, horizontal, sizeof( stbir__sampler ) );

      if ( copy_horizontal )
      {
        memcpy( &info->vertical, horizontal, sizeof( stbir__sampler ) );
      }
      else
      {

        stbir__calculate_filters( vertical, possibly_use_horizontal_for_pivot );
        memcpy( &info->vertical, vertical, sizeof( stbir__sampler ) );
      }

      // setup the vertical split ranges
      stbir__get_split_info( info->split_info, info->splits, info->vertical.scale_info.output_sub_size, info->vertical.filter_pixel_margin, info->vertical.scale_info.input_full_size );

      // now we know precisely how many entries we need
      info->ring_buffer_num_entries = info->vertical.extent_info.widest;

      // we never need more ring buffer entries than the scanlines we're outputting
      if ( ( !info->vertical.is_gather ) && ( info->ring_buffer_num_entries > conservative_split_output_size ) )
        info->ring_buffer_num_entries = conservative_split_output_size;
      assert( info->ring_buffer_num_entries <= info->alloc_ring_buffer_num_entries );
    }
    #undef STBIR__NEXT_PTR


    // is this the first time through loop?
    if ( info == 0 )
    {
      alloced_total = ( 15 + (size_t)advance_mem );
      alloced = malloc( alloced_total );
      if ( alloced == 0 )
        return 0;
    }
    else
      return info;  // success
  }
}

static void stbir__update_info_from_resize( ResizeInfo * info, ResizeData * resize )
{
  static stbir__decode_pixels_func * decode_simple[(size_t)DataType::HALF_FLOAT - (size_t)DataType::UINT8_SRGB+1]=
  {
    /* 1ch-4ch */ stbir__decode_uint8_srgb, stbir__decode_uint8_srgb, 0, stbir__decode_float_linear, stbir__decode_half_float_linear,
  };

  static stbir__decode_pixels_func * decode_alphas[STBIRI_AR-STBIRI_RGBA+1][(size_t)DataType::HALF_FLOAT - (size_t)DataType::UINT8_SRGB+1]=
  {
    { /* RGBA */ stbir__decode_uint8_srgb4_linearalpha,      stbir__decode_uint8_srgb,      0, stbir__decode_float_linear,      stbir__decode_half_float_linear },
    { /* BGRA */ stbir__decode_uint8_srgb4_linearalpha_BGRA, stbir__decode_uint8_srgb_BGRA, 0, stbir__decode_float_linear_BGRA, stbir__decode_half_float_linear_BGRA },
    { /* ARGB */ stbir__decode_uint8_srgb4_linearalpha_ARGB, stbir__decode_uint8_srgb_ARGB, 0, stbir__decode_float_linear_ARGB, stbir__decode_half_float_linear_ARGB },
    { /* ABGR */ stbir__decode_uint8_srgb4_linearalpha_ABGR, stbir__decode_uint8_srgb_ABGR, 0, stbir__decode_float_linear_ABGR, stbir__decode_half_float_linear_ABGR },
    { /* RA   */ stbir__decode_uint8_srgb2_linearalpha,      stbir__decode_uint8_srgb,      0, stbir__decode_float_linear,      stbir__decode_half_float_linear },
    { /* AR   */ stbir__decode_uint8_srgb2_linearalpha_AR,   stbir__decode_uint8_srgb_AR,   0, stbir__decode_float_linear_AR,   stbir__decode_half_float_linear_AR },
  };

  static stbir__decode_pixels_func * decode_simple_scaled_or_not[2][2]=
  {
    { stbir__decode_uint8_linear_scaled,  stbir__decode_uint8_linear }, { stbir__decode_uint16_linear_scaled, stbir__decode_uint16_linear },
  };

  static stbir__decode_pixels_func * decode_alphas_scaled_or_not[STBIRI_AR-STBIRI_RGBA+1][2][2]=
  {
    { /* RGBA */ { stbir__decode_uint8_linear_scaled,       stbir__decode_uint8_linear },      { stbir__decode_uint16_linear_scaled,      stbir__decode_uint16_linear } },
    { /* BGRA */ { stbir__decode_uint8_linear_scaled_BGRA,  stbir__decode_uint8_linear_BGRA }, { stbir__decode_uint16_linear_scaled_BGRA, stbir__decode_uint16_linear_BGRA } },
    { /* ARGB */ { stbir__decode_uint8_linear_scaled_ARGB,  stbir__decode_uint8_linear_ARGB }, { stbir__decode_uint16_linear_scaled_ARGB, stbir__decode_uint16_linear_ARGB } },
    { /* ABGR */ { stbir__decode_uint8_linear_scaled_ABGR,  stbir__decode_uint8_linear_ABGR }, { stbir__decode_uint16_linear_scaled_ABGR, stbir__decode_uint16_linear_ABGR } },
    { /* RA   */ { stbir__decode_uint8_linear_scaled,       stbir__decode_uint8_linear },      { stbir__decode_uint16_linear_scaled,      stbir__decode_uint16_linear } },
    { /* AR   */ { stbir__decode_uint8_linear_scaled_AR,    stbir__decode_uint8_linear_AR },   { stbir__decode_uint16_linear_scaled_AR,   stbir__decode_uint16_linear_AR } }
  };

  static stbir__encode_pixels_func * encode_simple[(size_t)DataType::HALF_FLOAT - (size_t)DataType::UINT8_SRGB+1]=
  {
    /* 1ch-4ch */ stbir__encode_uint8_srgb, stbir__encode_uint8_srgb, 0, stbir__encode_float_linear, stbir__encode_half_float_linear,
  };

  static stbir__encode_pixels_func * encode_alphas[STBIRI_AR-STBIRI_RGBA+1][(size_t)DataType::HALF_FLOAT - (size_t)DataType::UINT8_SRGB+1]=
  {
    { /* RGBA */ stbir__encode_uint8_srgb4_linearalpha,      stbir__encode_uint8_srgb,      0, stbir__encode_float_linear,      stbir__encode_half_float_linear },
    { /* BGRA */ stbir__encode_uint8_srgb4_linearalpha_BGRA, stbir__encode_uint8_srgb_BGRA, 0, stbir__encode_float_linear_BGRA, stbir__encode_half_float_linear_BGRA },
    { /* ARGB */ stbir__encode_uint8_srgb4_linearalpha_ARGB, stbir__encode_uint8_srgb_ARGB, 0, stbir__encode_float_linear_ARGB, stbir__encode_half_float_linear_ARGB },
    { /* ABGR */ stbir__encode_uint8_srgb4_linearalpha_ABGR, stbir__encode_uint8_srgb_ABGR, 0, stbir__encode_float_linear_ABGR, stbir__encode_half_float_linear_ABGR },
    { /* RA   */ stbir__encode_uint8_srgb2_linearalpha,      stbir__encode_uint8_srgb,      0, stbir__encode_float_linear,      stbir__encode_half_float_linear },
    { /* AR   */ stbir__encode_uint8_srgb2_linearalpha_AR,   stbir__encode_uint8_srgb_AR,   0, stbir__encode_float_linear_AR,   stbir__encode_half_float_linear_AR }
  };

  static stbir__encode_pixels_func * encode_simple_scaled_or_not[2][2]=
  {
    { stbir__encode_uint8_linear_scaled,  stbir__encode_uint8_linear }, { stbir__encode_uint16_linear_scaled, stbir__encode_uint16_linear },
  };

  static stbir__encode_pixels_func * encode_alphas_scaled_or_not[STBIRI_AR-STBIRI_RGBA+1][2][2]=
  {
    { /* RGBA */ { stbir__encode_uint8_linear_scaled,       stbir__encode_uint8_linear },       { stbir__encode_uint16_linear_scaled,      stbir__encode_uint16_linear } },
    { /* BGRA */ { stbir__encode_uint8_linear_scaled_BGRA,  stbir__encode_uint8_linear_BGRA },  { stbir__encode_uint16_linear_scaled_BGRA, stbir__encode_uint16_linear_BGRA } },
    { /* ARGB */ { stbir__encode_uint8_linear_scaled_ARGB,  stbir__encode_uint8_linear_ARGB },  { stbir__encode_uint16_linear_scaled_ARGB, stbir__encode_uint16_linear_ARGB } },
    { /* ABGR */ { stbir__encode_uint8_linear_scaled_ABGR,  stbir__encode_uint8_linear_ABGR },  { stbir__encode_uint16_linear_scaled_ABGR, stbir__encode_uint16_linear_ABGR } },
    { /* RA   */ { stbir__encode_uint8_linear_scaled,       stbir__encode_uint8_linear },       { stbir__encode_uint16_linear_scaled,      stbir__encode_uint16_linear } },
    { /* AR   */ { stbir__encode_uint8_linear_scaled_AR,    stbir__encode_uint8_linear_AR },    { stbir__encode_uint16_linear_scaled_AR,   stbir__encode_uint16_linear_AR } }
  };

  stbir__decode_pixels_func * decode_pixels = 0;
  stbir__encode_pixels_func * encode_pixels = 0;
  DataType input_type, output_type;

  input_type = resize->inputDataType;
  output_type = resize->outputDataType;
  info->input_data = resize->inputPixels;
  info->input_stride_bytes = resize->inputStrideInBytes;
  info->output_stride_bytes = resize->outputStrideInBytes;

  // recalc the output and input strides
  if ( info->input_stride_bytes == 0 )
    info->input_stride_bytes = info->channels * info->horizontal.scale_info.input_full_size * stbir__type_size[(size_t)input_type];

  if ( info->output_stride_bytes == 0 )
    info->output_stride_bytes = info->channels * info->horizontal.scale_info.output_sub_size * stbir__type_size[(size_t)output_type];

  // calc offset
  info->output_data = ( (char*) resize->outputPixels ) + ( (size_t) info->offset_y * (size_t) resize->outputStrideInBytes ) + ( info->offset_x * info->channels * stbir__type_size[(size_t)output_type] );

  info->user_data = resize->userData;

  // setup the input format converters
  if ( ( input_type == DataType::UINT8 ) || ( input_type == DataType::UINT16 ) )
  {
    int non_scaled = 0;

    // check if we can run unscaled - 0-255.0/0-65535.0 instead of 0-1.0 (which is a tiny bit faster when doing linear 8->8 or 16->16)
    if ( ( !info->alpha_weight ) && ( !info->alpha_unweight )  ) // don't short circuit when alpha weighting (get everything to 0-1.0 as usual)
      if ( ( ( input_type == DataType::UINT8 ) && ( output_type == DataType::UINT8 ) ) || ( ( input_type == DataType::UINT16 ) && ( output_type == DataType::UINT16 ) ) )
        non_scaled = 1;

    if ( info->input_pixel_layout_internal <= STBIRI_4CHANNEL )
      decode_pixels = decode_simple_scaled_or_not[ input_type == DataType::UINT16 ][ non_scaled ];
    else
      decode_pixels = decode_alphas_scaled_or_not[ ( info->input_pixel_layout_internal - STBIRI_RGBA ) % ( STBIRI_AR-STBIRI_RGBA+1 ) ][ input_type == DataType::UINT16 ][ non_scaled ];
  }
  else
  {
    if ( info->input_pixel_layout_internal <= STBIRI_4CHANNEL )
      decode_pixels = decode_simple[ (size_t)input_type - (size_t)DataType::UINT8_SRGB ];
    else
      decode_pixels = decode_alphas[ ( info->input_pixel_layout_internal - STBIRI_RGBA ) % ( STBIRI_AR-STBIRI_RGBA+1 ) ][ (size_t)input_type - (size_t)DataType::UINT8_SRGB ];
  }

  // setup the output format converters
  if ( ( output_type == DataType::UINT8 ) || ( output_type == DataType::UINT16 ) )
  {
    int non_scaled = 0;

    // check if we can run unscaled - 0-255.0/0-65535.0 instead of 0-1.0 (which is a tiny bit faster when doing linear 8->8 or 16->16)
    if ( ( !info->alpha_weight ) && ( !info->alpha_unweight ) ) // don't short circuit when alpha weighting (get everything to 0-1.0 as usual)
      if ( ( ( input_type == DataType::UINT8 ) && ( output_type == DataType::UINT8 ) ) || ( ( input_type == DataType::UINT16 ) && ( output_type == DataType::UINT16 ) ) )
        non_scaled = 1;

    if ( info->output_pixel_layout_internal <= STBIRI_4CHANNEL )
      encode_pixels = encode_simple_scaled_or_not[ output_type == DataType::UINT16 ][ non_scaled ];
    else
      encode_pixels = encode_alphas_scaled_or_not[ ( info->output_pixel_layout_internal - STBIRI_RGBA ) % ( STBIRI_AR-STBIRI_RGBA+1 ) ][ output_type == DataType::UINT16 ][ non_scaled ];
  }
  else
  {
    if ( info->output_pixel_layout_internal <= STBIRI_4CHANNEL )
      encode_pixels = encode_simple[ (size_t)output_type - (size_t)DataType::UINT8_SRGB ];
    else
      encode_pixels = encode_alphas[ ( info->output_pixel_layout_internal - STBIRI_RGBA ) % ( STBIRI_AR-STBIRI_RGBA+1 ) ][ (size_t)output_type - (size_t)DataType::UINT8_SRGB  ];
  }

  info->input_type = input_type;
  info->output_type = output_type;
  info->decode_pixels = decode_pixels;
  info->encode_pixels = encode_pixels;
}

#define STBIR_FORCE_MINIMUM_SCANLINES_FOR_SPLITS 4
static int stbir__perform_build( ResizeData * resize, int splits )
{
  stbir__contributors conservative = { 0, 0 };
  stbir__sampler horizontal, vertical;
  int new_output_subx, new_output_suby;
  ResizeInfo * out_info;

  // have we already built the samplers?
  if ( resize->samplers )
    return 0;

  new_output_subx = resize->outputSubX;
  new_output_suby = resize->outputSubY;

  // do horizontal clip and scale calcs
  if ( !stbir__calculate_region_transform( &horizontal.scale_info, resize->outputW, &new_output_subx, resize->outputSubW, resize->inputW, resize->inputS0, resize->inputS1 ) )
    return 0;

  // do vertical clip and scale calcs
  if ( !stbir__calculate_region_transform( &vertical.scale_info, resize->outputH, &new_output_suby, resize->outputSubH, resize->intputH, resize->inputT0, resize->inputT1 ) )
    return 0;

  // if nothing to do, just return
  if ( ( horizontal.scale_info.output_sub_size == 0 ) || ( vertical.scale_info.output_sub_size == 0 ) )
    return 0;

  stbir__set_sampler(&horizontal, &horizontal.scale_info, 1);
  stbir__get_conservative_extents( &horizontal, &conservative);
  stbir__set_sampler(&vertical, &vertical.scale_info, 0);

  if ( ( vertical.scale_info.output_sub_size / splits ) < STBIR_FORCE_MINIMUM_SCANLINES_FOR_SPLITS ) // each split should be a minimum of 4 scanlines (handwavey choice)
  {
    splits = vertical.scale_info.output_sub_size / STBIR_FORCE_MINIMUM_SCANLINES_FOR_SPLITS;
    if ( splits == 0 ) {
      splits = 1;
    }
  }

  out_info = stbir__alloc_internal_mem_and_build_samplers( &horizontal, &vertical, &conservative, resize->inputPixelLayoutPublic, resize->outputPixelLaytoutPublic, splits, new_output_subx, new_output_suby);

  if ( out_info )
  {
    resize->splits = splits;
    resize->samplers = out_info;
    resize->needsRebuild = 0;

    // update anything that can be changed without recalcing samplers
    stbir__update_info_from_resize( out_info, resize );

    return splits;
  }

  return 0;
}


static int stbir_build_samplers_with_splits( ResizeData * resize, int splits )
{
    resize->calledAlloc = 1;
    return stbir__perform_build( resize, splits );
}

static int stbir_build_samplers( ResizeData * resize )
{
  return stbir_build_samplers_with_splits( resize, 1 );
}

// Get the ring buffer pointer for an index
static float* stbir__get_ring_buffer_entry(ResizeInfo const * stbir_info, stbir__per_split_info const * split_info, int index )
{
  assert( index < stbir_info->ring_buffer_num_entries );
  return (float*) ( ( (char*) split_info->ring_buffer ) + ( index * stbir_info->ring_buffer_length_bytes ) );
}

// Get the specified scan line from the ring buffer
static float* stbir__get_ring_buffer_scanline(ResizeInfo const * stbir_info, stbir__per_split_info const * split_info, int get_scanline)
{
  int ring_buffer_index = (split_info->ring_buffer_begin_index + (get_scanline - split_info->ring_buffer_first_scanline)) % stbir_info->ring_buffer_num_entries;
  return stbir__get_ring_buffer_entry( stbir_info, split_info, ring_buffer_index );
}

static void stbir__decode_scanline(ResizeInfo const * stbir_info, int n, float * output_buffer)
{
  int channels = stbir_info->channels;
  int effective_channels = stbir_info->effective_channels;
  int input_sample_in_bytes = stbir__type_size[(size_t)stbir_info->input_type] * channels;
  int row = stbir__edge_wrap(n, stbir_info->vertical.scale_info.input_full_size);
  const void* input_plane_data = ( (char *) stbir_info->input_data ) + (size_t)row * (size_t) stbir_info->input_stride_bytes;
  stbir__span const * spans = stbir_info->scanline_extents.spans;
  float * full_decode_buffer = output_buffer - stbir_info->scanline_extents.conservative.n0 * effective_channels;
  float * last_decoded = 0;

  assert( !(n < 0 || n >= stbir_info->vertical.scale_info.input_full_size) );

  do
  {
    float * decode_buffer;
    void const * input_data;
    float * end_decode;
    int width_times_channels;
    int width;

    if ( spans->n1 < spans->n0 )
      break;

    width = spans->n1 + 1 - spans->n0;
    decode_buffer = full_decode_buffer + spans->n0 * effective_channels;
    end_decode = full_decode_buffer + ( spans->n1 + 1 ) * effective_channels;
    width_times_channels = width * channels;

    // read directly out of input plane by default
    input_data = ( (char*)input_plane_data ) + spans->pixel_offset_for_input * input_sample_in_bytes;

    // convert the pixels info the float decode_buffer, (we index from end_decode, so that when channels<effective_channels, we are right justified in the buffer)
    last_decoded = stbir_info->decode_pixels( (float*)end_decode - width_times_channels, width_times_channels, input_data );

    if (stbir_info->alpha_weight)
    {
      stbir_info->alpha_weight( decode_buffer, width_times_channels );
    }

    ++spans;
  } while ( spans <= ( &stbir_info->scanline_extents.spans[1] ) );

  // some of the horizontal gathers read one float off the edge (which is masked out), but we force a zero here to make sure no NaNs leak in
  //   (we can't pre-zero it, because the input callback can use that area as padding)
  last_decoded[0] = 0.0f;

  // we clear this extra float, because the final output pixel filter kernel might have used one less coeff than the max filter width
  //   when this happens, we do read that pixel from the input, so it too could be Nan, so just zero an extra one.
  //   this fits because each scanline is padded by three floats (STBIR_INPUT_CALLBACK_PADDING)
  last_decoded[1] = 0.0f;
}

static void stbir__resample_horizontal_gather(ResizeInfo const * stbir_info, float* output_buffer, float const * input_buffer)
{
  float const * decode_buffer = input_buffer - ( stbir_info->scanline_extents.conservative.n0 * stbir_info->effective_channels );
  stbir_info->horizontal_gather_channels( output_buffer, (unsigned int)stbir_info->horizontal.scale_info.output_sub_size, decode_buffer, stbir_info->horizontal.contributors, stbir_info->horizontal.coefficients, stbir_info->horizontal.coefficient_width );
}

static void stbir__decode_and_resample_for_vertical_gather_loop(ResizeInfo const * stbir_info, stbir__per_split_info* split_info, int n)
{
  int ring_buffer_index;
  float* ring_buffer;

  // Decode the nth scanline from the source image into the decode buffer.
  stbir__decode_scanline( stbir_info, n, split_info->decode_buffer);

  // update new end scanline
  split_info->ring_buffer_last_scanline = n;

  // get ring buffer
  ring_buffer_index = (split_info->ring_buffer_begin_index + (split_info->ring_buffer_last_scanline - split_info->ring_buffer_first_scanline)) % stbir_info->ring_buffer_num_entries;
  ring_buffer = stbir__get_ring_buffer_entry(stbir_info, split_info, ring_buffer_index);

  // Now resample it into the ring buffer.
  stbir__resample_horizontal_gather( stbir_info, ring_buffer, split_info->decode_buffer);

  // Now it's sitting in the ring buffer ready to be used as source for the vertical sampling.
}

typedef void STBIR_VERTICAL_GATHERFUNC( float * output, float const * coeffs, float const ** inputs, float const * input0_end );
static STBIR_VERTICAL_GATHERFUNC * stbir__vertical_gathers[ 8 ] =
{
  stbir__vertical_gather_with_1_coeffs,stbir__vertical_gather_with_2_coeffs,stbir__vertical_gather_with_3_coeffs,stbir__vertical_gather_with_4_coeffs,stbir__vertical_gather_with_5_coeffs,stbir__vertical_gather_with_6_coeffs,stbir__vertical_gather_with_7_coeffs,stbir__vertical_gather_with_8_coeffs
};

static STBIR_VERTICAL_GATHERFUNC * stbir__vertical_gathers_continues[ 8 ] =
{
  stbir__vertical_gather_with_1_coeffs_cont,stbir__vertical_gather_with_2_coeffs_cont,stbir__vertical_gather_with_3_coeffs_cont,stbir__vertical_gather_with_4_coeffs_cont,stbir__vertical_gather_with_5_coeffs_cont,stbir__vertical_gather_with_6_coeffs_cont,stbir__vertical_gather_with_7_coeffs_cont,stbir__vertical_gather_with_8_coeffs_cont
};

static void stbir__encode_scanline( ResizeInfo const * stbir_info, void *output_buffer_data, float * encode_buffer)
{
  int num_pixels = stbir_info->horizontal.scale_info.output_sub_size;
  int channels = stbir_info->channels;
  int width_times_channels = num_pixels * channels;
  void * output_buffer;

  // un-alpha weight if we need to
  if ( stbir_info->alpha_unweight )
  {
    stbir_info->alpha_unweight( encode_buffer, width_times_channels );
  }

  // write directly into output by default
  output_buffer = output_buffer_data;

  // convert into the output buffer
  stbir_info->encode_pixels( output_buffer, width_times_channels, encode_buffer );
}

static void stbir__resample_vertical_gather(ResizeInfo const * stbir_info, stbir__per_split_info* split_info, int n, int contrib_n0, int contrib_n1, float const * vertical_coefficients )
{
  float* encode_buffer = split_info->vertical_buffer;
  float* decode_buffer = split_info->decode_buffer;
  int vertical_first = stbir_info->vertical_first;
  int width = (vertical_first) ? ( stbir_info->scanline_extents.conservative.n1-stbir_info->scanline_extents.conservative.n0+1 ) : stbir_info->horizontal.scale_info.output_sub_size;
  int width_times_channels = stbir_info->effective_channels * width;

  assert( stbir_info->vertical.is_gather );

  // loop over the contributing scanlines and scale into the buffer
  {
    int k = 0, total = contrib_n1 - contrib_n0 + 1;
    assert( total > 0 );
    do {
      float const * inputs[8];
      int i, cnt = total; if ( cnt > 8 ) cnt = 8;
      for( i = 0 ; i < cnt ; i++ )
        inputs[ i ] = stbir__get_ring_buffer_scanline(stbir_info, split_info, k+i+contrib_n0 );

      // call the N scanlines at a time function (up to 8 scanlines of blending at once)
      ((k==0)?stbir__vertical_gathers:stbir__vertical_gathers_continues)[cnt-1]( (vertical_first) ? decode_buffer : encode_buffer, vertical_coefficients + k, inputs, inputs[0] + width_times_channels );
      k += cnt;
      total -= cnt;
    } while ( total );
  }

  if ( vertical_first )
  {
    // Now resample the gathered vertical data in the horizontal axis into the encode buffer
    decode_buffer[ width_times_channels ] = 0.0f; // clear two over for horizontals with a remnant of 3
    decode_buffer[ width_times_channels+1 ] = 0.0f;
    stbir__resample_horizontal_gather(stbir_info, encode_buffer, decode_buffer);
  }

  stbir__encode_scanline( stbir_info, ( (char *) stbir_info->output_data ) + ((size_t)n * (size_t)stbir_info->output_stride_bytes),
                          encode_buffer);
}

static void stbir__vertical_gather_loop( ResizeInfo const * stbir_info, stbir__per_split_info* split_info, int split_count )
{
  int y, start_output_y, end_output_y;
  stbir__contributors* vertical_contributors = stbir_info->vertical.contributors;
  float const * vertical_coefficients = stbir_info->vertical.coefficients;

  assert( stbir_info->vertical.is_gather );

  start_output_y = split_info->start_output_y;
  end_output_y = split_info[split_count-1].end_output_y;

  vertical_contributors += start_output_y;
  vertical_coefficients += start_output_y * stbir_info->vertical.coefficient_width;

  // initialize the ring buffer for gathering
  split_info->ring_buffer_begin_index = 0;
  split_info->ring_buffer_first_scanline = vertical_contributors->n0;
  split_info->ring_buffer_last_scanline = split_info->ring_buffer_first_scanline - 1; // means "empty"

  for (y = start_output_y; y < end_output_y; y++)
  {
    int in_first_scanline, in_last_scanline;

    in_first_scanline = vertical_contributors->n0;
    in_last_scanline = vertical_contributors->n1;

    // make sure the indexing hasn't broken
    assert( in_first_scanline >= split_info->ring_buffer_first_scanline );

    // Load in new scanlines
    while (in_last_scanline > split_info->ring_buffer_last_scanline)
    {
      assert( ( split_info->ring_buffer_last_scanline - split_info->ring_buffer_first_scanline + 1 ) <= stbir_info->ring_buffer_num_entries );

      // make sure there was room in the ring buffer when we add new scanlines
      if ( ( split_info->ring_buffer_last_scanline - split_info->ring_buffer_first_scanline + 1 ) == stbir_info->ring_buffer_num_entries )
      {
        split_info->ring_buffer_first_scanline++;
        split_info->ring_buffer_begin_index++;
      }

      if ( stbir_info->vertical_first )
      {
        float * ring_buffer = stbir__get_ring_buffer_scanline( stbir_info, split_info, ++split_info->ring_buffer_last_scanline );
        // Decode the nth scanline from the source image into the decode buffer.
        stbir__decode_scanline( stbir_info, split_info->ring_buffer_last_scanline, ring_buffer);
      }
      else
      {
        stbir__decode_and_resample_for_vertical_gather_loop(stbir_info, split_info, split_info->ring_buffer_last_scanline + 1);
      }
    }

    // Now all buffers should be ready to write a row of vertical sampling, so do it.
    stbir__resample_vertical_gather(stbir_info, split_info, y, in_first_scanline, in_last_scanline, vertical_coefficients );

    ++vertical_contributors;
    vertical_coefficients += stbir_info->vertical.coefficient_width;
  }
}
#define STBIR__FLOAT_EMPTY_MARKER 3.0e+38F
#define STBIR__FLOAT_BUFFER_IS_EMPTY(ptr) ((ptr)[0]==STBIR__FLOAT_EMPTY_MARKER)
static void stbir__horizontal_resample_and_encode_first_scanline_from_scatter(ResizeInfo const * stbir_info, stbir__per_split_info* split_info)
{
  // evict a scanline out into the output buffer

  float* ring_buffer_entry = stbir__get_ring_buffer_entry(stbir_info, split_info, split_info->ring_buffer_begin_index );

  // Now resample it into the buffer.
  stbir__resample_horizontal_gather( stbir_info, split_info->vertical_buffer, ring_buffer_entry);

  // dump the scanline out
  stbir__encode_scanline( stbir_info, ( (char *)stbir_info->output_data ) + ( (size_t)split_info->ring_buffer_first_scanline * (size_t)stbir_info->output_stride_bytes ), split_info->vertical_buffer);

  // mark it as empty
  ring_buffer_entry[ 0 ] = STBIR__FLOAT_EMPTY_MARKER;

  // advance the first scanline
  split_info->ring_buffer_first_scanline++;
  if ( ++split_info->ring_buffer_begin_index == stbir_info->ring_buffer_num_entries )
    split_info->ring_buffer_begin_index = 0;
}

static void stbir__encode_first_scanline_from_scatter(ResizeInfo const * stbir_info, stbir__per_split_info* split_info)
{
  // evict a scanline out into the output buffer
  float* ring_buffer_entry = stbir__get_ring_buffer_entry(stbir_info, split_info, split_info->ring_buffer_begin_index );

  // dump the scanline out
  stbir__encode_scanline( stbir_info, ( (char *)stbir_info->output_data ) + ( (size_t)split_info->ring_buffer_first_scanline * (size_t)stbir_info->output_stride_bytes ), ring_buffer_entry);

  // mark it as empty
  ring_buffer_entry[ 0 ] = STBIR__FLOAT_EMPTY_MARKER;

  // advance the first scanline
  split_info->ring_buffer_first_scanline++;
  if ( ++split_info->ring_buffer_begin_index == stbir_info->ring_buffer_num_entries )
    split_info->ring_buffer_begin_index = 0;
}

typedef void STBIR_VERTICAL_SCATTERFUNC( float ** outputs, float const * coeffs, float const * input, float const * input_end );
static STBIR_VERTICAL_SCATTERFUNC * stbir__vertical_scatter_sets[ 8 ] =
{
  stbir__vertical_scatter_with_1_coeffs,stbir__vertical_scatter_with_2_coeffs,stbir__vertical_scatter_with_3_coeffs,stbir__vertical_scatter_with_4_coeffs,stbir__vertical_scatter_with_5_coeffs,stbir__vertical_scatter_with_6_coeffs,stbir__vertical_scatter_with_7_coeffs,stbir__vertical_scatter_with_8_coeffs
};

static STBIR_VERTICAL_SCATTERFUNC * stbir__vertical_scatter_blends[ 8 ] =
{
  stbir__vertical_scatter_with_1_coeffs_cont,stbir__vertical_scatter_with_2_coeffs_cont,stbir__vertical_scatter_with_3_coeffs_cont,stbir__vertical_scatter_with_4_coeffs_cont,stbir__vertical_scatter_with_5_coeffs_cont,stbir__vertical_scatter_with_6_coeffs_cont,stbir__vertical_scatter_with_7_coeffs_cont,stbir__vertical_scatter_with_8_coeffs_cont
};


static void stbir__resample_vertical_scatter(ResizeInfo const * stbir_info, stbir__per_split_info* split_info, int n0, int n1, float const * vertical_coefficients, float const * vertical_buffer, float const * vertical_buffer_end )
{
  assert( !stbir_info->vertical.is_gather );

  {
    int k = 0, total = n1 - n0 + 1;
    assert( total > 0 );
    do {
      float * outputs[8];
      int i, n = total; if ( n > 8 ) n = 8;
      for( i = 0 ; i < n ; i++ )
      {
        outputs[ i ] = stbir__get_ring_buffer_scanline(stbir_info, split_info, k+i+n0 );
        if ( ( i ) && ( STBIR__FLOAT_BUFFER_IS_EMPTY( outputs[i] ) != STBIR__FLOAT_BUFFER_IS_EMPTY( outputs[0] ) ) ) // make sure runs are of the same type
        {
          n = i;
          break;
        }
      }
      // call the scatter to N scanlines at a time function (up to 8 scanlines of scattering at once)
      ((STBIR__FLOAT_BUFFER_IS_EMPTY( outputs[0] ))?stbir__vertical_scatter_sets:stbir__vertical_scatter_blends)[n-1]( outputs, vertical_coefficients + k, vertical_buffer, vertical_buffer_end );
      k += n;
      total -= n;
    } while ( total );
  }
}

typedef void stbir__handle_scanline_for_scatter_func(ResizeInfo const * stbir_info, stbir__per_split_info* split_info);
static void stbir__vertical_scatter_loop( ResizeInfo const * stbir_info, stbir__per_split_info* split_info, int split_count )
{
  int y, start_output_y, end_output_y, start_input_y, end_input_y;
  stbir__contributors* vertical_contributors = stbir_info->vertical.contributors;
  float const * vertical_coefficients = stbir_info->vertical.coefficients;
  stbir__handle_scanline_for_scatter_func * handle_scanline_for_scatter;
  void * scanline_scatter_buffer;
  void * scanline_scatter_buffer_end;
  int on_first_input_y, last_input_y;
  int width = (stbir_info->vertical_first) ? ( stbir_info->scanline_extents.conservative.n1-stbir_info->scanline_extents.conservative.n0+1 ) : stbir_info->horizontal.scale_info.output_sub_size;
  int width_times_channels = stbir_info->effective_channels * width;

  assert( !stbir_info->vertical.is_gather );

  start_output_y = split_info->start_output_y;
  end_output_y = split_info[split_count-1].end_output_y;  // may do multiple split counts

  start_input_y = split_info->start_input_y;
  end_input_y = split_info[split_count-1].end_input_y;

  // adjust for starting offset start_input_y
  y = start_input_y + stbir_info->vertical.filter_pixel_margin;
  vertical_contributors += y ;
  vertical_coefficients += stbir_info->vertical.coefficient_width * y;

  if ( stbir_info->vertical_first )
  {
    handle_scanline_for_scatter = stbir__horizontal_resample_and_encode_first_scanline_from_scatter;
    scanline_scatter_buffer = split_info->decode_buffer;
    scanline_scatter_buffer_end = ( (char*) scanline_scatter_buffer ) + sizeof( float ) * (unsigned long)stbir_info->effective_channels * (unsigned long)(stbir_info->scanline_extents.conservative.n1-stbir_info->scanline_extents.conservative.n0+1);
  }
  else
  {
    handle_scanline_for_scatter = stbir__encode_first_scanline_from_scatter;
    scanline_scatter_buffer = split_info->vertical_buffer;
    scanline_scatter_buffer_end = ( (char*) scanline_scatter_buffer ) + sizeof( float ) * (unsigned long)stbir_info->effective_channels * (unsigned long)stbir_info->horizontal.scale_info.output_sub_size;
  }

  // initialize the ring buffer for scattering
  split_info->ring_buffer_first_scanline = start_output_y;
  split_info->ring_buffer_last_scanline = -1;
  split_info->ring_buffer_begin_index = -1;

  // mark all the buffers as empty to start
  for( y = 0 ; y < stbir_info->ring_buffer_num_entries ; y++ )
  {
    float * decode_buffer = stbir__get_ring_buffer_entry( stbir_info, split_info, y );
    decode_buffer[ width_times_channels ] = 0.0f; // clear two over for horizontals with a remnant of 3
    decode_buffer[ width_times_channels+1 ] = 0.0f;
    decode_buffer[0] = STBIR__FLOAT_EMPTY_MARKER; // only used on scatter
  }

  // do the loop in input space
  on_first_input_y = 1; last_input_y = start_input_y;
  for (y = start_input_y ; y < end_input_y; y++)
  {
    int out_first_scanline, out_last_scanline;

    out_first_scanline = vertical_contributors->n0;
    out_last_scanline = vertical_contributors->n1;

    assert(out_last_scanline - out_first_scanline + 1 <= stbir_info->ring_buffer_num_entries);

    if ( ( out_last_scanline >= out_first_scanline ) && ( ( ( out_first_scanline >= start_output_y ) && ( out_first_scanline < end_output_y ) ) || ( ( out_last_scanline >= start_output_y ) && ( out_last_scanline < end_output_y ) ) ) )
    {
      float const * vc = vertical_coefficients;

      // keep track of the range actually seen for the next resize
      last_input_y = y;
      if ( ( on_first_input_y ) && ( y > start_input_y ) )
        split_info->start_input_y = y;
      on_first_input_y = 0;

      // clip the region
      if ( out_first_scanline < start_output_y )
      {
        vc += start_output_y - out_first_scanline;
        out_first_scanline = start_output_y;
      }

      if ( out_last_scanline >= end_output_y )
        out_last_scanline = end_output_y - 1;

      // if very first scanline, init the index
      if (split_info->ring_buffer_begin_index < 0)
        split_info->ring_buffer_begin_index = out_first_scanline - start_output_y;

      assert( split_info->ring_buffer_begin_index <= out_first_scanline );

      // Decode the nth scanline from the source image into the decode buffer.
      stbir__decode_scanline( stbir_info, y, split_info->decode_buffer);

      // When horizontal first, we resample horizontally into the vertical buffer before we scatter it out
      if ( !stbir_info->vertical_first )
        stbir__resample_horizontal_gather( stbir_info, split_info->vertical_buffer, split_info->decode_buffer);

      // Now it's sitting in the buffer ready to be distributed into the ring buffers.

      // evict from the ringbuffer, if we need are full
      if ( ( ( split_info->ring_buffer_last_scanline - split_info->ring_buffer_first_scanline + 1 ) == stbir_info->ring_buffer_num_entries ) &&
           ( out_last_scanline > split_info->ring_buffer_last_scanline ) )
        handle_scanline_for_scatter( stbir_info, split_info );

      // Now the horizontal buffer is ready to write to all ring buffer rows, so do it.
      stbir__resample_vertical_scatter(stbir_info, split_info, out_first_scanline, out_last_scanline, vc, (float*)scanline_scatter_buffer, (float*)scanline_scatter_buffer_end );

      // update the end of the buffer
      if ( out_last_scanline > split_info->ring_buffer_last_scanline )
        split_info->ring_buffer_last_scanline = out_last_scanline;
    }
    ++vertical_contributors;
    vertical_coefficients += stbir_info->vertical.coefficient_width;
  }

  // now evict the scanlines that are left over in the ring buffer
  while ( split_info->ring_buffer_first_scanline < end_output_y )
    handle_scanline_for_scatter(stbir_info, split_info);

  // update the end_input_y if we do multiple resizes with the same data
  ++last_input_y;
  for( y = 0 ; y < split_count; y++ )
    if ( split_info[y].end_input_y > last_input_y )
      split_info[y].end_input_y = last_input_y;
}

static int stbir__perform_resize( ResizeInfo const * info, int split_start, int split_count )
{
  stbir__per_split_info * split_info = info->split_info + split_start;

  if (info->vertical.is_gather)
    stbir__vertical_gather_loop( info, split_info, split_count );
  else
    stbir__vertical_scatter_loop( info, split_info, split_count );

  return 1;
}

static int stbir_resize_extended( ResizeData * resize )
{
  int result;
  // remember allocated state
  int alloc_state = resize->calledAlloc;

  if ( !stbir_build_samplers( resize ) )
    return 0;

  resize->calledAlloc = alloc_state;

  // if build_samplers succeeded (above), but there are no samplers set, then
  //   the area to stretch into was zero pixels, so don't do anything and return
  //   success
  if ( resize->samplers == 0 )
    return 1;

  // do resize
  result = stbir__perform_resize( resize->samplers, 0, resize->splits );

  // if we alloced, then free
  if ( !resize->calledAlloc )
  {
    stbir_free_samplers( resize );
    resize->samplers = 0;
  }

  return result;
}

static bool ImageResize(const void* inputPixels, int inputW, int inputH, int inputStrideInBytes,
    void* outputPixels, int outputW, int outputH, int outputStrideInBytes, PixelLayout pixelLayout,
    DataType dataType) {
  ResizeData resize;
  float* optr;
  int opitch;
  if(!stbir__check_output_stuff((void**)&optr, &opitch, outputPixels, stbir__type_size[static_cast<size_t>(dataType)], outputW, outputH, outputStrideInBytes, stbir__pixel_layout_convert_public_to_internal[static_cast<size_t>(pixelLayout)])) {
    return false;
  }
  stbir_resize_init(&resize, inputPixels, inputW, inputH, inputStrideInBytes,
    (optr)? optr : outputPixels, outputW, outputH, outputStrideInBytes, pixelLayout, dataType);

  if(!stbir_resize_extended(&resize)) {
    if(optr) {
      free(optr);
    }
    return false;
  }
  return true;
}


static void ConvertDataTypeAndChannel(ColorType colorType, DataType* dataType,
                                    PixelLayout* pixelLayout) {
  switch (colorType) {
    case ColorType::ALPHA_8:
    case ColorType::Gray_8: {
      *pixelLayout = PixelLayout::CHANNEL_1;
      *dataType = DataType::UINT8;
      break;
    }
    case ColorType::RGB_565: {
      *pixelLayout = PixelLayout::RGB;
      *dataType = DataType::UINT8;
      break;
    }
    default: {
      break;
    }
  }
}

bool ImageResize(const void* inputPixels, const ImageInfo& inputInfo, void* outputPixels, const ImageInfo& outputInfo) {
  Buffer dstTempBuffer = {};
  Buffer srcTempBuffer = {};
  auto dataType = DataType::UINT8;
  auto channel = PixelLayout::RGBA;
  auto dstImageInfo = outputInfo;
  auto srcImageInfo = inputInfo;
  auto srcData = inputPixels;
  auto dstData = outputPixels;
  if (inputInfo.colorType() == ColorType::RGBA_1010102 ||
      inputInfo.colorType() == ColorType::RGBA_F16) {
    srcImageInfo = inputInfo.makeColorType(ColorType::RGBA_8888);
    srcTempBuffer.alloc(srcImageInfo.byteSize());
    Pixmap(inputInfo, inputPixels).readPixels(srcImageInfo, srcTempBuffer.bytes());
    srcData = srcTempBuffer.data();
  }
  if (srcImageInfo.colorType() != outputInfo.colorType()) {
    dstImageInfo = outputInfo.makeColorType(srcImageInfo.colorType());
    dstTempBuffer.alloc(dstImageInfo.byteSize());
    dstData = dstTempBuffer.data();
  }
  ConvertDataTypeAndChannel(srcImageInfo.colorType(), &dataType, &channel);

  auto result = ImageResize(srcData, srcImageInfo.width(), srcImageInfo.height(), static_cast<int>(srcImageInfo.rowBytes()),
    dstData, dstImageInfo.width(), dstImageInfo.height(), static_cast<int>(dstImageInfo.rowBytes()), channel, dataType);
  if (!result) {
    return false;
  }
  if (!dstTempBuffer.isEmpty()) {
    Pixmap(dstImageInfo, dstData).readPixels(outputInfo, outputPixels);
  }
  return true;
}
}  // namespace tgfx
#else

// we reinclude the header file to define all the horizontal functions
//   specializing each function for the number of coeffs is 20-40% faster *OVERALL*

// by including the source file again this way, we can still debug the functions

#define STBIR_strs_join2( start, mid, end ) start##mid##end
#define STBIR_strs_join1( start, mid, end ) STBIR_strs_join2( start, mid, end )

#define STBIR_strs_join24( start, mid1, mid2, end ) start##mid1##mid2##end
#define STBIR_strs_join14( start, mid1, mid2, end ) STBIR_strs_join24( start, mid1, mid2, end )

#ifdef STB_IMAGE_RESIZE_DO_CODERS
#ifdef stbir__decode_suffix
#define STBIR__CODER_NAME( name ) STBIR_strs_join1( name, _, stbir__decode_suffix )
#else
#define STBIR__CODER_NAME( name ) name
#endif

#ifndef stbir__decode_swizzle
#define stbir__decode_order0 0
#define stbir__decode_order1 1
#define stbir__decode_order2 2
#define stbir__decode_order3 3
#define stbir__encode_order0 0
#define stbir__encode_order1 1
#define stbir__encode_order2 2
#define stbir__encode_order3 3
#endif

static float * STBIR__CODER_NAME( stbir__decode_uint8_linear_scaled )( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const*)inputp;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = ((float)(input[stbir__decode_order0])) * stbir__max_uint8_as_float_inverted;
    decode[1-4] = ((float)(input[stbir__decode_order1])) * stbir__max_uint8_as_float_inverted;
    decode[2-4] = ((float)(input[stbir__decode_order2])) * stbir__max_uint8_as_float_inverted;
    decode[3-4] = ((float)(input[stbir__decode_order3])) * stbir__max_uint8_as_float_inverted;
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = ((float)(input[stbir__decode_order0])) * stbir__max_uint8_as_float_inverted;
    #if stbir__coder_min_num >= 2
    decode[1] = ((float)(input[stbir__decode_order1])) * stbir__max_uint8_as_float_inverted;
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = ((float)(input[stbir__decode_order2])) * stbir__max_uint8_as_float_inverted;
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif

  return decode_end;
}

static void STBIR__CODER_NAME( stbir__encode_uint8_linear_scaled )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char *) outputp;
  unsigned char * end_output = ( (unsigned char *) output ) + width_times_channels;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  while( output <= end_output )
  {
    float f;
    f = encode[stbir__encode_order0] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[0-4] = (unsigned char)f;
    f = encode[stbir__encode_order1] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[1-4] = (unsigned char)f;
    f = encode[stbir__encode_order2] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[2-4] = (unsigned char)f;
    f = encode[stbir__encode_order3] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[3-4] = (unsigned char)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float f;
    STBIR_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[0] = (unsigned char)f;
    #if stbir__coder_min_num >= 2
    f = encode[stbir__encode_order1] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[1] = (unsigned char)f;
    #endif
    #if stbir__coder_min_num >= 3
    f = encode[stbir__encode_order2] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[2] = (unsigned char)f;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_uint8_linear)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const*)inputp;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = ((float)(input[stbir__decode_order0]));
    decode[1-4] = ((float)(input[stbir__decode_order1]));
    decode[2-4] = ((float)(input[stbir__decode_order2]));
    decode[3-4] = ((float)(input[stbir__decode_order3]));
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = ((float)(input[stbir__decode_order0]));
    #if stbir__coder_min_num >= 2
    decode[1] = ((float)(input[stbir__decode_order1]));
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = ((float)(input[stbir__decode_order2]));
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;
}

static void STBIR__CODER_NAME( stbir__encode_uint8_linear )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char *) outputp;
  unsigned char * end_output = ( (unsigned char *) output ) + width_times_channels;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  while( output <= end_output )
  {
    float f;
    f = encode[stbir__encode_order0] + 0.5f; STBIR_CLAMP(f, 0, 255); output[0-4] = (unsigned char)f;
    f = encode[stbir__encode_order1] + 0.5f; STBIR_CLAMP(f, 0, 255); output[1-4] = (unsigned char)f;
    f = encode[stbir__encode_order2] + 0.5f; STBIR_CLAMP(f, 0, 255); output[2-4] = (unsigned char)f;
    f = encode[stbir__encode_order3] + 0.5f; STBIR_CLAMP(f, 0, 255); output[3-4] = (unsigned char)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float f;
    STBIR_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] + 0.5f; STBIR_CLAMP(f, 0, 255); output[0] = (unsigned char)f;
    #if stbir__coder_min_num >= 2
    f = encode[stbir__encode_order1] + 0.5f; STBIR_CLAMP(f, 0, 255); output[1] = (unsigned char)f;
    #endif
    #if stbir__coder_min_num >= 3
    f = encode[stbir__encode_order2] + 0.5f; STBIR_CLAMP(f, 0, 255); output[2] = (unsigned char)f;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_uint8_srgb)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const *)inputp;

  // try to do blocks of 4 when you can
#if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  while( decode <= decode_end )
  {
    decode[0-4] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order0 ] ];
    decode[1-4] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order1 ] ];
    decode[2-4] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order2 ] ];
    decode[3-4] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order3 ] ];
    decode += 4;
    input += 4;
  }
  decode -= 4;
#endif

  // do the remnants
#if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order0 ] ];
#if stbir__coder_min_num >= 2
    decode[1] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order1 ] ];
#endif
#if stbir__coder_min_num >= 3
    decode[2] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order2 ] ];
#endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
#endif
  return decode_end;
}

#define stbir__min_max_shift20( i, f ) \
    stbir__simdf_max( f, f, stbir_simdf_casti(STBIR__CONSTI( STBIR_almost_zero )) ); \
    stbir__simdf_min( f, f, stbir_simdf_casti(STBIR__CONSTI( STBIR_almost_one  )) ); \
    stbir__simdi_32shr( i, stbir_simdi_castf( f ), 20 );

#define stbir__scale_and_convert( i, f ) \
    stbir__simdf_madd( f, STBIR__CONSTF( STBIR_simd_point5 ), STBIR__CONSTF( STBIR_max_uint8_as_float ), f ); \
    stbir__simdf_max( f, f, stbir__simdf_zeroP() ); \
    stbir__simdf_min( f, f, STBIR__CONSTF( STBIR_max_uint8_as_float ) ); \
    stbir__simdf_convert_float_to_i32( i, f );

#define stbir__linear_to_srgb_finish( i, f ) \
{ \
    stbir__simdi temp;  \
    stbir__simdi_32shr( temp, stbir_simdi_castf( f ), 12 ) ; \
    stbir__simdi_and( temp, temp, STBIR__CONSTI(STBIR_mastissa_mask) ); \
    stbir__simdi_or( temp, temp, STBIR__CONSTI(STBIR_topscale) ); \
    stbir__simdi_16madd( i, i, temp ); \
    stbir__simdi_32shr( i, i, 16 ); \
}

#define stbir__simdi_table_lookup2( v0,v1, table ) \
{ \
  stbir__simdi_u32 temp0,temp1; \
  temp0.m128i_i128 = v0; \
  temp1.m128i_i128 = v1; \
  temp0.m128i_u32[0] = table[temp0.m128i_i32[0]]; temp0.m128i_u32[1] = table[temp0.m128i_i32[1]]; temp0.m128i_u32[2] = table[temp0.m128i_i32[2]]; temp0.m128i_u32[3] = table[temp0.m128i_i32[3]]; \
  temp1.m128i_u32[0] = table[temp1.m128i_i32[0]]; temp1.m128i_u32[1] = table[temp1.m128i_i32[1]]; temp1.m128i_u32[2] = table[temp1.m128i_i32[2]]; temp1.m128i_u32[3] = table[temp1.m128i_i32[3]]; \
  v0 = temp0.m128i_i128; \
  v1 = temp1.m128i_i128; \
}

#define stbir__simdi_table_lookup3( v0,v1,v2, table ) \
{ \
  stbir__simdi_u32 temp0,temp1,temp2; \
  temp0.m128i_i128 = v0; \
  temp1.m128i_i128 = v1; \
  temp2.m128i_i128 = v2; \
  temp0.m128i_u32[0] = table[temp0.m128i_i32[0]]; temp0.m128i_u32[1] = table[temp0.m128i_i32[1]]; temp0.m128i_u32[2] = table[temp0.m128i_i32[2]]; temp0.m128i_u32[3] = table[temp0.m128i_i32[3]]; \
  temp1.m128i_u32[0] = table[temp1.m128i_i32[0]]; temp1.m128i_u32[1] = table[temp1.m128i_i32[1]]; temp1.m128i_u32[2] = table[temp1.m128i_i32[2]]; temp1.m128i_u32[3] = table[temp1.m128i_i32[3]]; \
  temp2.m128i_u32[0] = table[temp2.m128i_i32[0]]; temp2.m128i_u32[1] = table[temp2.m128i_i32[1]]; temp2.m128i_u32[2] = table[temp2.m128i_i32[2]]; temp2.m128i_u32[3] = table[temp2.m128i_i32[3]]; \
  v0 = temp0.m128i_i128; \
  v1 = temp1.m128i_i128; \
  v2 = temp2.m128i_i128; \
}

#define stbir__simdi_table_lookup4( v0,v1,v2,v3, table ) \
{ \
  stbir__simdi_u32 temp0,temp1,temp2,temp3; \
  temp0.m128i_i128 = v0; \
  temp1.m128i_i128 = v1; \
  temp2.m128i_i128 = v2; \
  temp3.m128i_i128 = v3; \
  temp0.m128i_u32[0] = table[temp0.m128i_i32[0]]; temp0.m128i_u32[1] = table[temp0.m128i_i32[1]]; temp0.m128i_u32[2] = table[temp0.m128i_i32[2]]; temp0.m128i_u32[3] = table[temp0.m128i_i32[3]]; \
  temp1.m128i_u32[0] = table[temp1.m128i_i32[0]]; temp1.m128i_u32[1] = table[temp1.m128i_i32[1]]; temp1.m128i_u32[2] = table[temp1.m128i_i32[2]]; temp1.m128i_u32[3] = table[temp1.m128i_i32[3]]; \
  temp2.m128i_u32[0] = table[temp2.m128i_i32[0]]; temp2.m128i_u32[1] = table[temp2.m128i_i32[1]]; temp2.m128i_u32[2] = table[temp2.m128i_i32[2]]; temp2.m128i_u32[3] = table[temp2.m128i_i32[3]]; \
  temp3.m128i_u32[0] = table[temp3.m128i_i32[0]]; temp3.m128i_u32[1] = table[temp3.m128i_i32[1]]; temp3.m128i_u32[2] = table[temp3.m128i_i32[2]]; temp3.m128i_u32[3] = table[temp3.m128i_i32[3]]; \
  v0 = temp0.m128i_i128; \
  v1 = temp1.m128i_i128; \
  v2 = temp2.m128i_i128; \
  v3 = temp3.m128i_i128; \
}

static void STBIR__CODER_NAME( stbir__encode_uint8_srgb )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char*) outputp;
  unsigned char * end_output = ( (unsigned char*) output ) + width_times_channels;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while ( output <= end_output )
  {
    STBIR_SIMD_NO_UNROLL(encode);

    output[0-4] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order0] );
    output[1-4] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order1] );
    output[2-4] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order2] );
    output[3-4] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order3] );

    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    STBIR_NO_UNROLL(encode);
    output[0] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order0] );
    #if stbir__coder_min_num >= 2
    output[1] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order1] );
    #endif
    #if stbir__coder_min_num >= 3
    output[2] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order2] );
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

#if ( stbir__coder_min_num == 4 ) || ( ( stbir__coder_min_num == 1 ) && ( !defined(stbir__decode_swizzle) ) )

static float * STBIR__CODER_NAME(stbir__decode_uint8_srgb4_linearalpha)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const *)inputp;

  do {
    decode[0] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order0] ];
    decode[1] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order1] ];
    decode[2] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order2] ];
    decode[3] = ( (float) input[stbir__decode_order3] ) * stbir__max_uint8_as_float_inverted;
    input += 4;
    decode += 4;
  } while( decode < decode_end );
  return decode_end;
}

static void STBIR__CODER_NAME( stbir__encode_uint8_srgb4_linearalpha )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char*) outputp;
  unsigned char * end_output = ( (unsigned char*) output ) + width_times_channels;

  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float f;
    STBIR_SIMD_NO_UNROLL(encode);

    output[stbir__decode_order0] = stbir__linear_to_srgb_uchar( encode[0] );
    output[stbir__decode_order1] = stbir__linear_to_srgb_uchar( encode[1] );
    output[stbir__decode_order2] = stbir__linear_to_srgb_uchar( encode[2] );

    f = encode[3] * stbir__max_uint8_as_float + 0.5f;
    STBIR_CLAMP(f, 0, 255);
    output[stbir__decode_order3] = (unsigned char) f;

    output += 4;
    encode += 4;
  } while( output < end_output );
}
#endif

#if ( stbir__coder_min_num == 2 ) || ( ( stbir__coder_min_num == 1 ) && ( !defined(stbir__decode_swizzle) ) )

static float * STBIR__CODER_NAME(stbir__decode_uint8_srgb2_linearalpha)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const *)inputp;

  decode += 4;
  while( decode <= decode_end )
  {
    decode[0-4] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order0] ];
    decode[1-4] = ( (float) input[stbir__decode_order1] ) * stbir__max_uint8_as_float_inverted;
    decode[2-4] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order0+2] ];
    decode[3-4] = ( (float) input[stbir__decode_order1+2] ) * stbir__max_uint8_as_float_inverted;
    input += 4;
    decode += 4;
  }
  decode -= 4;
  if( decode < decode_end )
  {
    decode[0] = stbir__srgb_uchar_to_linear_float[ stbir__decode_order0 ];
    decode[1] = ( (float) input[stbir__decode_order1] ) * stbir__max_uint8_as_float_inverted;
  }
  return decode_end;
}

static void STBIR__CODER_NAME( stbir__encode_uint8_srgb2_linearalpha )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char*) outputp;
  unsigned char * end_output = ( (unsigned char*) output ) + width_times_channels;

  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float f;
    STBIR_SIMD_NO_UNROLL(encode);

    output[stbir__decode_order0] = stbir__linear_to_srgb_uchar( encode[0] );

    f = encode[1] * stbir__max_uint8_as_float + 0.5f;
    STBIR_CLAMP(f, 0, 255);
    output[stbir__decode_order1] = (unsigned char) f;

    output += 2;
    encode += 2;
  } while( output < end_output );
}

#endif

static float * STBIR__CODER_NAME(stbir__decode_uint16_linear_scaled)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned short const * input = (unsigned short const *)inputp;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = ((float)(input[stbir__decode_order0])) * stbir__max_uint16_as_float_inverted;
    decode[1-4] = ((float)(input[stbir__decode_order1])) * stbir__max_uint16_as_float_inverted;
    decode[2-4] = ((float)(input[stbir__decode_order2])) * stbir__max_uint16_as_float_inverted;
    decode[3-4] = ((float)(input[stbir__decode_order3])) * stbir__max_uint16_as_float_inverted;
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = ((float)(input[stbir__decode_order0])) * stbir__max_uint16_as_float_inverted;
    #if stbir__coder_min_num >= 2
    decode[1] = ((float)(input[stbir__decode_order1])) * stbir__max_uint16_as_float_inverted;
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = ((float)(input[stbir__decode_order2])) * stbir__max_uint16_as_float_inverted;
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;
}

static void STBIR__CODER_NAME(stbir__encode_uint16_linear_scaled)( void * outputp, int width_times_channels, float const * encode )
{
  unsigned short STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned short*) outputp;
  unsigned short * end_output = ( (unsigned short*) output ) + width_times_channels;


  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    float f;
    STBIR_SIMD_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[0-4] = (unsigned short)f;
    f = encode[stbir__encode_order1] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[1-4] = (unsigned short)f;
    f = encode[stbir__encode_order2] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[2-4] = (unsigned short)f;
    f = encode[stbir__encode_order3] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[3-4] = (unsigned short)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float f;
    STBIR_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[0] = (unsigned short)f;
    #if stbir__coder_min_num >= 2
    f = encode[stbir__encode_order1] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[1] = (unsigned short)f;
    #endif
    #if stbir__coder_min_num >= 3
    f = encode[stbir__encode_order2] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[2] = (unsigned short)f;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_uint16_linear)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned short const * input = (unsigned short const *)inputp;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = ((float)(input[stbir__decode_order0]));
    decode[1-4] = ((float)(input[stbir__decode_order1]));
    decode[2-4] = ((float)(input[stbir__decode_order2]));
    decode[3-4] = ((float)(input[stbir__decode_order3]));
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = ((float)(input[stbir__decode_order0]));
    #if stbir__coder_min_num >= 2
    decode[1] = ((float)(input[stbir__decode_order1]));
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = ((float)(input[stbir__decode_order2]));
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;
}

static void STBIR__CODER_NAME(stbir__encode_uint16_linear)( void * outputp, int width_times_channels, float const * encode )
{
  unsigned short STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned short*) outputp;
  unsigned short * end_output = ( (unsigned short*) output ) + width_times_channels;

  // try to do blocks of 4 when you can
  #if  stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    float f;
    STBIR_SIMD_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[0-4] = (unsigned short)f;
    f = encode[stbir__encode_order1] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[1-4] = (unsigned short)f;
    f = encode[stbir__encode_order2] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[2-4] = (unsigned short)f;
    f = encode[stbir__encode_order3] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[3-4] = (unsigned short)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float f;
    STBIR_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[0] = (unsigned short)f;
    #if stbir__coder_min_num >= 2
    f = encode[stbir__encode_order1] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[1] = (unsigned short)f;
    #endif
    #if stbir__coder_min_num >= 3
    f = encode[stbir__encode_order2] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[2] = (unsigned short)f;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_half_float_linear)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  stbir__FP16 const * input = (stbir__FP16 const *)inputp;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = stbir__half_to_float(input[stbir__decode_order0]);
    decode[1-4] = stbir__half_to_float(input[stbir__decode_order1]);
    decode[2-4] = stbir__half_to_float(input[stbir__decode_order2]);
    decode[3-4] = stbir__half_to_float(input[stbir__decode_order3]);
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = stbir__half_to_float(input[stbir__decode_order0]);
    #if stbir__coder_min_num >= 2
    decode[1] = stbir__half_to_float(input[stbir__decode_order1]);
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = stbir__half_to_float(input[stbir__decode_order2]);
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;
}

static void STBIR__CODER_NAME( stbir__encode_half_float_linear )( void * outputp, int width_times_channels, float const * encode )
{
  stbir__FP16 STBIR_SIMD_STREAMOUT_PTR( * ) output = (stbir__FP16*) outputp;
  stbir__FP16 * end_output = ( (stbir__FP16*) output ) + width_times_channels;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    STBIR_SIMD_NO_UNROLL(output);
    output[0-4] = stbir__float_to_half(encode[stbir__encode_order0]);
    output[1-4] = stbir__float_to_half(encode[stbir__encode_order1]);
    output[2-4] = stbir__float_to_half(encode[stbir__encode_order2]);
    output[3-4] = stbir__float_to_half(encode[stbir__encode_order3]);
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    STBIR_NO_UNROLL(output);
    output[0] = stbir__float_to_half(encode[stbir__encode_order0]);
    #if stbir__coder_min_num >= 2
    output[1] = stbir__float_to_half(encode[stbir__encode_order1]);
    #endif
    #if stbir__coder_min_num >= 3
    output[2] = stbir__float_to_half(encode[stbir__encode_order2]);
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_float_linear)( float * decodep, int width_times_channels, void const * inputp )
{
  #ifdef stbir__decode_swizzle
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  float const * input = (float const *)inputp;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = input[stbir__decode_order0];
    decode[1-4] = input[stbir__decode_order1];
    decode[2-4] = input[stbir__decode_order2];
    decode[3-4] = input[stbir__decode_order3];
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = input[stbir__decode_order0];
    #if stbir__coder_min_num >= 2
    decode[1] = input[stbir__decode_order1];
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = input[stbir__decode_order2];
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;

  #else

  if ( (void*)decodep != inputp )
    memcpy( decodep, inputp, (size_t)width_times_channels * sizeof( float ) );

  return decodep + width_times_channels;

  #endif
}

static void STBIR__CODER_NAME( stbir__encode_float_linear )( void * outputp, int width_times_channels, float const * encode )
{
  #if !defined( STBIR_FLOAT_HIGH_CLAMP ) && !defined(STBIR_FLOAT_LO_CLAMP) && !defined(stbir__decode_swizzle)

  if ( (void*)outputp != (void*) encode )
    memcpy( outputp, encode, (size_t)width_times_channels * sizeof( float ) );

  #else

  float STBIR_SIMD_STREAMOUT_PTR( * ) output = (float*) outputp;
  float * end_output = ( (float*) output ) + width_times_channels;

  #ifdef STBIR_FLOAT_HIGH_CLAMP
  #define stbir_scalar_hi_clamp( v ) if ( v > STBIR_FLOAT_HIGH_CLAMP ) v = STBIR_FLOAT_HIGH_CLAMP;
  #else
  #define stbir_scalar_hi_clamp( v )
  #endif
  #ifdef STBIR_FLOAT_LOW_CLAMP
  #define stbir_scalar_lo_clamp( v ) if ( v < STBIR_FLOAT_LOW_CLAMP ) v = STBIR_FLOAT_LOW_CLAMP;
  #else
  #define stbir_scalar_lo_clamp( v )
  #endif

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    float e;
    STBIR_SIMD_NO_UNROLL(encode);
    e = encode[ stbir__encode_order0 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[0-4] = e;
    e = encode[ stbir__encode_order1 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[1-4] = e;
    e = encode[ stbir__encode_order2 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[2-4] = e;
    e = encode[ stbir__encode_order3 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[3-4] = e;
    output += 4;
    encode += 4;
  }
  output -= 4;

  #endif


  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float e;
    STBIR_NO_UNROLL(encode);
    e = encode[ stbir__encode_order0 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[0] = e;
    #if stbir__coder_min_num >= 2
    e = encode[ stbir__encode_order1 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[1] = e;
    #endif
    #if stbir__coder_min_num >= 3
    e = encode[ stbir__encode_order2 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[2] = e;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif

  #endif
}

#undef stbir__decode_suffix
#undef stbir__decode_order0
#undef stbir__decode_order1
#undef stbir__decode_order2
#undef stbir__decode_order3
#undef stbir__encode_order0
#undef stbir__encode_order1
#undef stbir__encode_order2
#undef stbir__encode_order3
#undef STBIR__CODER_NAME
#undef stbir__coder_min_num
#undef stbir__decode_swizzle
#undef stbir_scalar_hi_clamp
#undef stbir_scalar_lo_clamp
#undef STB_IMAGE_RESIZE_DO_CODERS

#elif defined( STB_IMAGE_RESIZE_DO_VERTICALS)

#ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#define STBIR_chans( start, end ) STBIR_strs_join14(start,STBIR__vertical_channels,end,_cont)
#else
#define STBIR_chans( start, end ) STBIR_strs_join1(start,STBIR__vertical_channels,end)
#endif

#if STBIR__vertical_channels >= 1
#define stbIF0( code ) code
#else
#define stbIF0( code )
#endif
#if STBIR__vertical_channels >= 2
#define stbIF1( code ) code
#else
#define stbIF1( code )
#endif
#if STBIR__vertical_channels >= 3
#define stbIF2( code ) code
#else
#define stbIF2( code )
#endif
#if STBIR__vertical_channels >= 4
#define stbIF3( code ) code
#else
#define stbIF3( code )
#endif
#if STBIR__vertical_channels >= 5
#define stbIF4( code ) code
#else
#define stbIF4( code )
#endif
#if STBIR__vertical_channels >= 6
#define stbIF5( code ) code
#else
#define stbIF5( code )
#endif
#if STBIR__vertical_channels >= 7
#define stbIF6( code ) code
#else
#define stbIF6( code )
#endif
#if STBIR__vertical_channels >= 8
#define stbIF7( code ) code
#else
#define stbIF7( code )
#endif

static void STBIR_chans( stbir__vertical_scatter_with_,_coeffs)( float ** outputs, float const * vertical_coefficients, float const * input, float const * input_end )
{
  stbIF0( float STBIR_SIMD_STREAMOUT_PTR( * ) output0 = outputs[0]; float c0s = vertical_coefficients[0]; )
  stbIF1( float STBIR_SIMD_STREAMOUT_PTR( * ) output1 = outputs[1]; float c1s = vertical_coefficients[1]; )
  stbIF2( float STBIR_SIMD_STREAMOUT_PTR( * ) output2 = outputs[2]; float c2s = vertical_coefficients[2]; )
  stbIF3( float STBIR_SIMD_STREAMOUT_PTR( * ) output3 = outputs[3]; float c3s = vertical_coefficients[3]; )
  stbIF4( float STBIR_SIMD_STREAMOUT_PTR( * ) output4 = outputs[4]; float c4s = vertical_coefficients[4]; )
  stbIF5( float STBIR_SIMD_STREAMOUT_PTR( * ) output5 = outputs[5]; float c5s = vertical_coefficients[5]; )
  stbIF6( float STBIR_SIMD_STREAMOUT_PTR( * ) output6 = outputs[6]; float c6s = vertical_coefficients[6]; )
  stbIF7( float STBIR_SIMD_STREAMOUT_PTR( * ) output7 = outputs[7]; float c7s = vertical_coefficients[7]; )

  STBIR_NO_UNROLL_LOOP_START
  while ( ( (char*)input_end - (char*) input ) >= 16 )
  {
    float r0, r1, r2, r3;
    STBIR_NO_UNROLL(input);

    r0 = input[0], r1 = input[1], r2 = input[2], r3 = input[3];

    #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
    stbIF0( output0[0] += ( r0 * c0s ); output0[1] += ( r1 * c0s ); output0[2] += ( r2 * c0s ); output0[3] += ( r3 * c0s ); )
    stbIF1( output1[0] += ( r0 * c1s ); output1[1] += ( r1 * c1s ); output1[2] += ( r2 * c1s ); output1[3] += ( r3 * c1s ); )
    stbIF2( output2[0] += ( r0 * c2s ); output2[1] += ( r1 * c2s ); output2[2] += ( r2 * c2s ); output2[3] += ( r3 * c2s ); )
    stbIF3( output3[0] += ( r0 * c3s ); output3[1] += ( r1 * c3s ); output3[2] += ( r2 * c3s ); output3[3] += ( r3 * c3s ); )
    stbIF4( output4[0] += ( r0 * c4s ); output4[1] += ( r1 * c4s ); output4[2] += ( r2 * c4s ); output4[3] += ( r3 * c4s ); )
    stbIF5( output5[0] += ( r0 * c5s ); output5[1] += ( r1 * c5s ); output5[2] += ( r2 * c5s ); output5[3] += ( r3 * c5s ); )
    stbIF6( output6[0] += ( r0 * c6s ); output6[1] += ( r1 * c6s ); output6[2] += ( r2 * c6s ); output6[3] += ( r3 * c6s ); )
    stbIF7( output7[0] += ( r0 * c7s ); output7[1] += ( r1 * c7s ); output7[2] += ( r2 * c7s ); output7[3] += ( r3 * c7s ); )
    #else
    stbIF0( output0[0]  = ( r0 * c0s ); output0[1]  = ( r1 * c0s ); output0[2]  = ( r2 * c0s ); output0[3]  = ( r3 * c0s ); )
    stbIF1( output1[0]  = ( r0 * c1s ); output1[1]  = ( r1 * c1s ); output1[2]  = ( r2 * c1s ); output1[3]  = ( r3 * c1s ); )
    stbIF2( output2[0]  = ( r0 * c2s ); output2[1]  = ( r1 * c2s ); output2[2]  = ( r2 * c2s ); output2[3]  = ( r3 * c2s ); )
    stbIF3( output3[0]  = ( r0 * c3s ); output3[1]  = ( r1 * c3s ); output3[2]  = ( r2 * c3s ); output3[3]  = ( r3 * c3s ); )
    stbIF4( output4[0]  = ( r0 * c4s ); output4[1]  = ( r1 * c4s ); output4[2]  = ( r2 * c4s ); output4[3]  = ( r3 * c4s ); )
    stbIF5( output5[0]  = ( r0 * c5s ); output5[1]  = ( r1 * c5s ); output5[2]  = ( r2 * c5s ); output5[3]  = ( r3 * c5s ); )
    stbIF6( output6[0]  = ( r0 * c6s ); output6[1]  = ( r1 * c6s ); output6[2]  = ( r2 * c6s ); output6[3]  = ( r3 * c6s ); )
    stbIF7( output7[0]  = ( r0 * c7s ); output7[1]  = ( r1 * c7s ); output7[2]  = ( r2 * c7s ); output7[3]  = ( r3 * c7s ); )
    #endif

    input += 4;
    stbIF0( output0 += 4; ) stbIF1( output1 += 4; ) stbIF2( output2 += 4; ) stbIF3( output3 += 4; ) stbIF4( output4 += 4; ) stbIF5( output5 += 4; ) stbIF6( output6 += 4; ) stbIF7( output7 += 4; )
  }
  STBIR_NO_UNROLL_LOOP_START
  while ( input < input_end )
  {
    float r = input[0];
    STBIR_NO_UNROLL(output0);

    #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
    stbIF0( output0[0] += ( r * c0s ); )
    stbIF1( output1[0] += ( r * c1s ); )
    stbIF2( output2[0] += ( r * c2s ); )
    stbIF3( output3[0] += ( r * c3s ); )
    stbIF4( output4[0] += ( r * c4s ); )
    stbIF5( output5[0] += ( r * c5s ); )
    stbIF6( output6[0] += ( r * c6s ); )
    stbIF7( output7[0] += ( r * c7s ); )
    #else
    stbIF0( output0[0]  = ( r * c0s ); )
    stbIF1( output1[0]  = ( r * c1s ); )
    stbIF2( output2[0]  = ( r * c2s ); )
    stbIF3( output3[0]  = ( r * c3s ); )
    stbIF4( output4[0]  = ( r * c4s ); )
    stbIF5( output5[0]  = ( r * c5s ); )
    stbIF6( output6[0]  = ( r * c6s ); )
    stbIF7( output7[0]  = ( r * c7s ); )
    #endif

    ++input;
    stbIF0( ++output0; ) stbIF1( ++output1; ) stbIF2( ++output2; ) stbIF3( ++output3; ) stbIF4( ++output4; ) stbIF5( ++output5; ) stbIF6( ++output6; ) stbIF7( ++output7; )
  }
}

static void STBIR_chans( stbir__vertical_gather_with_,_coeffs)( float * outputp, float const * vertical_coefficients, float const ** inputs, float const * input0_end )
{
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = outputp;

  stbIF0( float const * input0 = inputs[0]; float c0s = vertical_coefficients[0]; )
  stbIF1( float const * input1 = inputs[1]; float c1s = vertical_coefficients[1]; )
  stbIF2( float const * input2 = inputs[2]; float c2s = vertical_coefficients[2]; )
  stbIF3( float const * input3 = inputs[3]; float c3s = vertical_coefficients[3]; )
  stbIF4( float const * input4 = inputs[4]; float c4s = vertical_coefficients[4]; )
  stbIF5( float const * input5 = inputs[5]; float c5s = vertical_coefficients[5]; )
  stbIF6( float const * input6 = inputs[6]; float c6s = vertical_coefficients[6]; )
  stbIF7( float const * input7 = inputs[7]; float c7s = vertical_coefficients[7]; )

#if ( STBIR__vertical_channels == 1 ) && !defined(STB_IMAGE_RESIZE_VERTICAL_CONTINUE)
  // check single channel one weight
  if ( ( c0s >= (1.0f-0.000001f) ) && ( c0s <= (1.0f+0.000001f) ) )
  {
    memcpy( output, input0, (size_t)((char*)input0_end - (char*)input0 ));
    return;
  }
#endif

  STBIR_NO_UNROLL_LOOP_START
  while ( ( (char*)input0_end - (char*) input0 ) >= 16 )
  {
    float o0, o1, o2, o3;
    STBIR_NO_UNROLL(output);
    #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
    stbIF0( o0 = output[0] + input0[0] * c0s; o1 = output[1] + input0[1] * c0s; o2 = output[2] + input0[2] * c0s; o3 = output[3] + input0[3] * c0s; )
    #else
    stbIF0( o0  = input0[0] * c0s; o1  = input0[1] * c0s; o2  = input0[2] * c0s; o3  = input0[3] * c0s; )
    #endif
    stbIF1( o0 += input1[0] * c1s; o1 += input1[1] * c1s; o2 += input1[2] * c1s; o3 += input1[3] * c1s; )
    stbIF2( o0 += input2[0] * c2s; o1 += input2[1] * c2s; o2 += input2[2] * c2s; o3 += input2[3] * c2s; )
    stbIF3( o0 += input3[0] * c3s; o1 += input3[1] * c3s; o2 += input3[2] * c3s; o3 += input3[3] * c3s; )
    stbIF4( o0 += input4[0] * c4s; o1 += input4[1] * c4s; o2 += input4[2] * c4s; o3 += input4[3] * c4s; )
    stbIF5( o0 += input5[0] * c5s; o1 += input5[1] * c5s; o2 += input5[2] * c5s; o3 += input5[3] * c5s; )
    stbIF6( o0 += input6[0] * c6s; o1 += input6[1] * c6s; o2 += input6[2] * c6s; o3 += input6[3] * c6s; )
    stbIF7( o0 += input7[0] * c7s; o1 += input7[1] * c7s; o2 += input7[2] * c7s; o3 += input7[3] * c7s; )
    output[0] = o0; output[1] = o1; output[2] = o2; output[3] = o3;
    output += 4;
    stbIF0( input0 += 4; ) stbIF1( input1 += 4; ) stbIF2( input2 += 4; ) stbIF3( input3 += 4; ) stbIF4( input4 += 4; ) stbIF5( input5 += 4; ) stbIF6( input6 += 4; ) stbIF7( input7 += 4; )
  }
  STBIR_NO_UNROLL_LOOP_START
  while ( input0 < input0_end )
  {
    float o0;
    STBIR_NO_UNROLL(output);
    #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
    stbIF0( o0 = output[0] + input0[0] * c0s; )
    #else
    stbIF0( o0  = input0[0] * c0s; )
    #endif
    stbIF1( o0 += input1[0] * c1s; )
    stbIF2( o0 += input2[0] * c2s; )
    stbIF3( o0 += input3[0] * c3s; )
    stbIF4( o0 += input4[0] * c4s; )
    stbIF5( o0 += input5[0] * c5s; )
    stbIF6( o0 += input6[0] * c6s; )
    stbIF7( o0 += input7[0] * c7s; )
    output[0] = o0;
    ++output;
    stbIF0( ++input0; ) stbIF1( ++input1; ) stbIF2( ++input2; ) stbIF3( ++input3; ) stbIF4( ++input4; ) stbIF5( ++input5; ) stbIF6( ++input6; ) stbIF7( ++input7; )
  }
}

#undef stbIF0
#undef stbIF1
#undef stbIF2
#undef stbIF3
#undef stbIF4
#undef stbIF5
#undef stbIF6
#undef stbIF7
#undef STB_IMAGE_RESIZE_DO_VERTICALS
#undef STBIR__vertical_channels
#undef STB_IMAGE_RESIZE_DO_HORIZONTALS
#undef STBIR_strs_join24
#undef STBIR_strs_join14
#undef STBIR_chans
#ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#undef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#endif

#else

#define STBIR_chans( start, end ) STBIR_strs_join1(start,STBIR__horizontal_channels,end)

#ifndef stbir__2_coeff_only
#define stbir__2_coeff_only()             \
stbir__1_coeff_only();                \
stbir__1_coeff_remnant(1);
#endif

#ifndef stbir__2_coeff_remnant
#define stbir__2_coeff_remnant( ofs )     \
stbir__1_coeff_remnant(ofs);          \
stbir__1_coeff_remnant((ofs)+1);
#endif

#ifndef stbir__3_coeff_only
#define stbir__3_coeff_only()             \
stbir__2_coeff_only();                \
stbir__1_coeff_remnant(2);
#endif

#ifndef stbir__3_coeff_remnant
#define stbir__3_coeff_remnant( ofs )     \
stbir__2_coeff_remnant(ofs);          \
stbir__1_coeff_remnant((ofs)+2);
#endif

#ifndef stbir__3_coeff_setup
#define stbir__3_coeff_setup()
#endif

#ifndef stbir__4_coeff_start
#define stbir__4_coeff_start()            \
stbir__2_coeff_only();                \
stbir__2_coeff_remnant(2);
#endif

#ifndef stbir__4_coeff_continue_from_4
#define stbir__4_coeff_continue_from_4( ofs )     \
stbir__2_coeff_remnant(ofs);                  \
stbir__2_coeff_remnant((ofs)+2);
#endif

#ifndef stbir__store_output_tiny
#define stbir__store_output_tiny stbir__store_output
#endif

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_1_coeff)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__1_coeff_only();
    stbir__store_output_tiny();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_2_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__2_coeff_only();
    stbir__store_output_tiny();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_3_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__3_coeff_only();
    stbir__store_output_tiny();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_4_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_5_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__1_coeff_remnant(4);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_6_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__2_coeff_remnant(4);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_7_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  stbir__3_coeff_setup();
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    stbir__3_coeff_remnant(4);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_8_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_9_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__1_coeff_remnant(8);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_10_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__2_coeff_remnant(8);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_11_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  stbir__3_coeff_setup();
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__3_coeff_remnant(8);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_12_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__4_coeff_continue_from_4(8);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_n_coeffs_mod0 )( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    int n = ( ( horizontal_contributors->n1 - horizontal_contributors->n0 + 1 ) - 4 + 3 ) >> 2;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    STBIR_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += STBIR__horizontal_channels * 4;
      stbir__4_coeff_continue_from_4( 0 );
      --n;
    } while ( n > 0 );
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_n_coeffs_mod1 )( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    int n = ( ( horizontal_contributors->n1 - horizontal_contributors->n0 + 1 ) - 5 + 3 ) >> 2;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    STBIR_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += STBIR__horizontal_channels * 4;
      stbir__4_coeff_continue_from_4( 0 );
      --n;
    } while ( n > 0 );
    stbir__1_coeff_remnant( 4 );
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_n_coeffs_mod2 )( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    int n = ( ( horizontal_contributors->n1 - horizontal_contributors->n0 + 1 ) - 6 + 3 ) >> 2;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    STBIR_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += STBIR__horizontal_channels * 4;
      stbir__4_coeff_continue_from_4( 0 );
      --n;
    } while ( n > 0 );
    stbir__2_coeff_remnant( 4 );

    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_n_coeffs_mod3 )( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  stbir__3_coeff_setup();
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    int n = ( ( horizontal_contributors->n1 - horizontal_contributors->n0 + 1 ) - 7 + 3 ) >> 2;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    STBIR_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += STBIR__horizontal_channels * 4;
      stbir__4_coeff_continue_from_4( 0 );
      --n;
    } while ( n > 0 );
    stbir__3_coeff_remnant( 4 );

    stbir__store_output();
  } while ( output < output_end );
}

static stbir__horizontal_gather_channels_func * STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_funcs)[4]=
{
  STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_mod0),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_mod1),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_mod2),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_mod3),
};

static stbir__horizontal_gather_channels_func * STBIR_chans(stbir__horizontal_gather_,_channels_funcs)[12]=
{
  STBIR_chans(stbir__horizontal_gather_,_channels_with_1_coeff),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_2_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_3_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_4_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_5_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_6_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_7_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_8_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_9_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_10_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_11_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_12_coeffs),
};

#undef STBIR__horizontal_channels
#undef STB_IMAGE_RESIZE_DO_HORIZONTALS
#undef stbir__1_coeff_only
#undef stbir__1_coeff_remnant
#undef stbir__2_coeff_only
#undef stbir__2_coeff_remnant
#undef stbir__3_coeff_only
#undef stbir__3_coeff_remnant
#undef stbir__3_coeff_setup
#undef stbir__4_coeff_start
#undef stbir__4_coeff_continue_from_4
#undef stbir__store_output
#undef stbir__store_output_tiny
#undef STBIR_chans

#endif

#endif