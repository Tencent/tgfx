/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "FloatToDecimal.h"
#include <cfloat>
#include <climits>
#include <cmath>
#include "core/utils/Log.h"

namespace tgfx {

namespace {
// returns `value * pow(base, e)`, assuming `e` is positive.
double pow_by_squaring(double value, double base, int e) {
  // https://en.wikipedia.org/wiki/Exponentiation_by_squaring
  DEBUG_ASSERT(e > 0);
  while (true) {
    if (e & 1) {
      value *= base;
    }
    e >>= 1;
    if (0 == e) {
      return value;
    }
    base *= base;
  }
}

// Return pow(10.0, e), optimized for common cases.
double pow10(int e) {
  switch (e) {
    case 0:
      return 1.0;  // common cases
    case 1:
      return 10.0;
    case 2:
      return 100.0;
    case 3:
      return 1e+03;
    case 4:
      return 1e+04;
    case 5:
      return 1e+05;
    case 6:
      return 1e+06;
    case 7:
      return 1e+07;
    case 8:
      return 1e+08;
    case 9:
      return 1e+09;
    case 10:
      return 1e+10;
    case 11:
      return 1e+11;
    case 12:
      return 1e+12;
    case 13:
      return 1e+13;
    case 14:
      return 1e+14;
    case 15:
      return 1e+15;
    default:
      if (e > 15) {
        return pow_by_squaring(1e+15, 10.0, e - 15);
      } else {
        DEBUG_ASSERT(e < 0);
        return pow_by_squaring(1.0, 0.1, -e);
      }
  }
}
}  // namespace

/** Write a string into output, including a terminating '\0' (for
    unit testing).  Return strlen(output) The
    resulting string will be in the form /[-]?([0-9]*.)?[0-9]+/ and
    sscanf(output, "%f", &x) will return the original value iff the
    value is finite. This function accepts all possible input values.

    Motivation: "PDF does not support [numbers] in exponential format
    (such as 6.02e23)."  Otherwise, this function would rely on a
    sprintf-type function from the standard library. */
unsigned FloatToDecimal(float value, char output[MaximumFloatToDecimalLength]) {
  /* The longest result is -FLT_MIN.
       We serialize it as "-.0000000000000000000000000000000000000117549435"
       which has 48 characters plus a terminating '\0'. */

  static_assert(MaximumFloatToDecimalLength == 49);
  // 3 = '-', '.', and '\0' characters.
  // 9 = number of significant digits
  // abs(FLT_MIN_10_EXP) = number of zeros in FLT_MIN
  static_assert(MaximumFloatToDecimalLength == 3 + 9 - FLT_MIN_10_EXP);

  /* section C.1 of the PDF1.4 spec (http://goo.gl/0SCswJ) says that
       most PDF rasterizers will use fixed-point scalars that lack the
       dynamic range of floats.  Even if this is the case, I want to
       serialize these (uncommon) very small and very large scalar
       values with enough precision to allow a floating-point
       rasterizer to read them in with perfect accuracy.
       Experimentally, rasterizers such as pdfium do seem to benefit
       from this.  Rasterizers that rely on fixed-point scalars should
       gracefully ignore these values that they can not parse. */
  char* output_ptr = &output[0];
  const char* const end = &output[MaximumFloatToDecimalLength - 1];
  // subtract one to leave space for '\0'.

  /* This function is written to accept any possible input value,
       including non-finite values such as INF and NAN.  In that case,
       we ignore value-correctness and output a syntacticly-valid
       number. */
  if (value == INFINITY) {
    value = FLT_MAX;  // nearest finite float.
  }
  if (value == -INFINITY) {
    value = -FLT_MAX;  // nearest finite float.
  }
  if (!std::isfinite(value) || value == 0.0f) {
    // NAN is unsupported in PDF.  Always output a valid number.
    // Also catch zero here, as a special case.
    *output_ptr++ = '0';
    *output_ptr = '\0';
    return static_cast<unsigned>(output_ptr - output);
  }
  if (value < 0.0) {
    *output_ptr++ = '-';
    value = -value;
  }
  DEBUG_ASSERT(value >= 0.0f);

  int binaryExponent;
  (void)std::frexp(value, &binaryExponent);
  static const double kLog2 = 0.3010299956639812;  // log10(2.0);
  int decimalExponent = static_cast<int>(std::floor(kLog2 * binaryExponent));
  int decimalShift = decimalExponent - 8;
  double power = pow10(-decimalShift);
  DEBUG_ASSERT(value * power <= (double)INT_MAX);
  auto d = static_cast<int>(std::lround(value * power));

  DEBUG_ASSERT(d <= 999999999);
  // floor(pow(10,1+log10(1<<24)))
  if (d > 167772159) {
    // need one fewer decimal digits for 24-bit precision.
    decimalShift = decimalExponent - 7;
    // recalculate to get rounding right.
    d = static_cast<int>(std::lround(value * (power * 0.1)));
    DEBUG_ASSERT(d <= 99999999);
  }
  while (d % 10 == 0) {
    d /= 10;
    ++decimalShift;
  }
  DEBUG_ASSERT(d > 0);
  unsigned char buffer[9];  // decimal value buffer.
  int bufferIndex = 0;
  do {
    buffer[bufferIndex++] = static_cast<unsigned char>(d % 10);
    d /= 10;
  } while (d != 0);
  DEBUG_ASSERT((bufferIndex <= (int)sizeof(buffer) && bufferIndex > 0));
  if (decimalShift >= 0) {
    do {
      --bufferIndex;
      *output_ptr++ = static_cast<char>('0' + buffer[bufferIndex]);
    } while (bufferIndex);
    for (int i = 0; i < decimalShift; ++i) {
      *output_ptr++ = '0';
    }
  } else {
    int placesBeforeDecimal = bufferIndex + decimalShift;
    if (placesBeforeDecimal > 0) {
      while (placesBeforeDecimal-- > 0) {
        --bufferIndex;
        *output_ptr++ = static_cast<char>('0' + buffer[bufferIndex]);
      }
      *output_ptr++ = '.';
    } else {
      *output_ptr++ = '.';
      int placesAfterDecimal = -placesBeforeDecimal;
      while (placesAfterDecimal-- > 0) {
        *output_ptr++ = '0';
      }
    }
    while (bufferIndex > 0) {
      --bufferIndex;
      *output_ptr++ = static_cast<char>('0' + buffer[bufferIndex]);
      if (output_ptr == end) {
        break;  // denormalized: don't need extra precision.
                // Note: denormalized numbers will not have the same number of
                // significantDigits, but do not need them to round-trip.
      }
    }
  }
  DEBUG_ASSERT(output_ptr <= end);
  *output_ptr = '\0';
  return static_cast<unsigned>(output_ptr - output);
}

}  // namespace tgfx