#version 150
layout(std140) uniform FragmentUniformBlock {
     vec4 Color_P0;
     vec4 Kernel_P1[17];
     int Radius_P1;
     vec2 Step_P1;
     vec4 Clamp_P2;
     vec2 Dimension_P2;
};
uniform sampler2D TextureSampler_0_P1;

in vec2 TransformedCoords_0_P0;

out vec4 tgfx_FragColor;

vec4 tiledApplyBorder(vec4 color, int modeX, int modeY, vec4 subsetRect, vec2 inCoord,
                      vec2 subsetCoord, vec2 clampedCoord) {
  if (modeX == 8) {
    float errX = subsetCoord.x - clampedCoord.x;
    color = mix(color, vec4(0.0), min(abs(errX), 1.0));
  }
  if (modeY == 8) {
    float errY = subsetCoord.y - clampedCoord.y;
    color = mix(color, vec4(0.0), min(abs(errY), 1.0));
  }
  if (modeX == 7) {
    float snappedX = floor(inCoord.x + 0.001) + 0.5;
    if (snappedX < subsetRect.x || snappedX > subsetRect.z) {
      color = vec4(0.0);
    }
  }
  if (modeY == 7) {
    float snappedY = floor(inCoord.y + 0.001) + 0.5;
    if (snappedY < subsetRect.y || snappedY > subsetRect.w) {
      color = vec4(0.0);
    }
  }
  return color;
}

vec2 tiledMapCoord(vec3 texCoord, bool strict, int modeX, int modeY, vec4 subsetRect,
                   vec4 clampRect, vec2 dimension, out vec2 inCoordOut, out vec2 subsetCoordOut,
                   out vec2 clampedCoordOut) {
  bool unormX = modeX == 3 || modeX == 4 || modeX == 5 || modeX == 7 || modeX == 8;
  bool unormY = modeY == 3 || modeY == 4 || modeY == 5 || modeY == 7 || modeY == 8;
  vec2 inCoord = texCoord.xy / texCoord.z;
  if (unormX || unormY) {
    inCoord /= dimension;
  }
  vec2 subsetCoord;
  if (modeX == 2 || modeX == 3) {
    subsetCoord.x = mod(inCoord.x - subsetRect.x, subsetRect.z - subsetRect.x) + subsetRect.x;
  } else if (modeX == 6) {
    float widthX = subsetRect.z - subsetRect.x;
    float mirroredX = mod(inCoord.x - subsetRect.x, 2.0 * widthX);
    subsetCoord.x = mix(mirroredX, 2.0 * widthX - mirroredX, step(widthX, mirroredX)) + subsetRect.x;
  } else {
    subsetCoord.x = inCoord.x;
  }
  if (modeY == 2 || modeY == 3) {
    subsetCoord.y = mod(inCoord.y - subsetRect.y, subsetRect.w - subsetRect.y) + subsetRect.y;
  } else if (modeY == 6) {
    float widthY = subsetRect.w - subsetRect.y;
    float mirroredY = mod(inCoord.y - subsetRect.y, 2.0 * widthY);
    subsetCoord.y = mix(mirroredY, 2.0 * widthY - mirroredY, step(widthY, mirroredY)) + subsetRect.y;
  } else {
    subsetCoord.y = inCoord.y;
  }
  bool clampX =
      modeX == 1 || modeX == 2 || modeX == 3 || modeX == 4 || modeX == 5 || modeX == 6 || modeX == 8;
  bool clampY =
      modeY == 1 || modeY == 2 || modeY == 3 || modeY == 4 || modeY == 5 || modeY == 6 || modeY == 8;
  vec2 clampedCoord;
  clampedCoord.x = clampX ? clamp(subsetCoord.x, clampRect.x, clampRect.z) : subsetCoord.x;
  clampedCoord.y = clampY ? clamp(subsetCoord.y, clampRect.y, clampRect.w) : subsetCoord.y;
  if (strict) {
    clampedCoord = clamp(clampedCoord, clampRect.xy, clampRect.zw);
  }
  inCoordOut = inCoord;
  subsetCoordOut = subsetCoord;
  clampedCoordOut = clampedCoord;
  return (unormX || unormY) ? clampedCoord * dimension : clampedCoord;
}

vec4 tgfxTextureSample(sampler2D tex, highp vec2 coord, highp vec4 subset, int clampOn,
                       int readSwizzle) {
  highp vec2 finalCoord = clampOn != 0 ? clamp(coord, subset.xy, subset.zw) : coord;
  vec4 color = texture(tex, finalCoord);
  if (readSwizzle == 1) {
    return color.rrrr;
  }
  if (readSwizzle == 2) {
    return color.rrra;
  }
  if (readSwizzle == 3) {
    return color.rgrg;
  }
  if (readSwizzle == 4) {
    return color.aaaa;
  }
  if (readSwizzle == 5) {
    return color.bgra;
  }
  return color;
}

vec4 tiledSeamBlend(sampler2D texSampler, vec4 texColor, int modeX, int modeY, vec4 clampRect,
                    vec2 dimension, vec2 inCoord, vec2 subsetCoord, vec2 clampedCoord,
                    int readSwizzle) {
  bool repeatX = modeX == 3 && subsetCoord.x != clampedCoord.x;
  bool repeatY = modeY == 3 && subsetCoord.y != clampedCoord.y;
  if (!repeatX && !repeatY) {
    return texColor;
  }
  float errX = subsetCoord.x - clampedCoord.x;
  float errY = subsetCoord.y - clampedCoord.y;
  float repeatCoordX = errX > 0.0 ? clampRect.x : clampRect.z;
  float repeatCoordY = errY > 0.0 ? clampRect.y : clampRect.w;
  if (repeatX && repeatY) {
    vec4 repeatReadX =
        tgfxTextureSample(texSampler, vec2(repeatCoordX, clampedCoord.y) * dimension, vec4(0.0), 0, readSwizzle);
    vec4 repeatReadY =
        tgfxTextureSample(texSampler, vec2(clampedCoord.x, repeatCoordY) * dimension, vec4(0.0), 0, readSwizzle);
    vec4 repeatReadXY =
        tgfxTextureSample(texSampler, vec2(repeatCoordX, repeatCoordY) * dimension, vec4(0.0), 0, readSwizzle);
    return mix(mix(texColor, repeatReadX, abs(errX)), mix(repeatReadY, repeatReadXY, abs(errX)),
               abs(errY));
  }
  if (repeatX) {
    vec4 repeatReadX =
        tgfxTextureSample(texSampler, vec2(repeatCoordX, clampedCoord.y) * dimension, vec4(0.0), 0, readSwizzle);
    return mix(texColor, repeatReadX, errX);
  }
  vec4 repeatReadY =
      tgfxTextureSample(texSampler, vec2(clampedCoord.x, repeatCoordY) * dimension, vec4(0.0), 0, readSwizzle);
  return mix(texColor, repeatReadY, errY);
}

vec4 gaussianBlur1D(sampler2D sourceSampler, highp vec2 baseCoord, highp vec2 tapStep, int tapRadius,
                    highp vec4 sourceSubset, int subsetEnabled, int tiledSource, int tiledModeX,
                    int tiledModeY, highp vec4 tiledSubset, highp vec4 tiledClamp,
                    highp vec2 tiledDimension, int seamBlend, int borderPass, vec4 kernelSlot0, vec4 kernelSlot1, vec4 kernelSlot2, vec4 kernelSlot3, vec4 kernelSlot4, vec4 kernelSlot5, vec4 kernelSlot6, vec4 kernelSlot7, vec4 kernelSlot8, vec4 kernelSlot9, vec4 kernelSlot10, vec4 kernelSlot11, vec4 kernelSlot12, vec4 kernelSlot13, vec4 kernelSlot14, vec4 kernelSlot15, vec4 kernelSlot16) {
  vec4 sum = vec4(0.0);
  for (int step = 0; step <= 40; ++step) {
    int tap = step - tapRadius;
    if (tap > tapRadius) {
      break;
    }
    int kernelOffset = abs(tap);
    int kernelSlot = kernelOffset / 4;
    vec4 kernelValues = kernelSlot0;
    if (kernelSlot == 0) { kernelValues = kernelSlot0; }
    if (kernelSlot == 1) { kernelValues = kernelSlot1; }
    if (kernelSlot == 2) { kernelValues = kernelSlot2; }
    if (kernelSlot == 3) { kernelValues = kernelSlot3; }
    if (kernelSlot == 4) { kernelValues = kernelSlot4; }
    if (kernelSlot == 5) { kernelValues = kernelSlot5; }
    if (kernelSlot == 6) { kernelValues = kernelSlot6; }
    if (kernelSlot == 7) { kernelValues = kernelSlot7; }
    if (kernelSlot == 8) { kernelValues = kernelSlot8; }
    if (kernelSlot == 9) { kernelValues = kernelSlot9; }
    if (kernelSlot == 10) { kernelValues = kernelSlot10; }
    if (kernelSlot == 11) { kernelValues = kernelSlot11; }
    if (kernelSlot == 12) { kernelValues = kernelSlot12; }
    if (kernelSlot == 13) { kernelValues = kernelSlot13; }
    if (kernelSlot == 14) { kernelValues = kernelSlot14; }
    if (kernelSlot == 15) { kernelValues = kernelSlot15; }
    if (kernelSlot == 16) { kernelValues = kernelSlot16; }
    int kernelLane = kernelOffset - kernelSlot * 4;
    float weight = kernelValues.x;
    if (kernelLane == 1) { weight = kernelValues.y; }
    if (kernelLane == 2) { weight = kernelValues.z; }
    if (kernelLane == 3) { weight = kernelValues.w; }
    vec2 sampleCoord = baseCoord + tapStep * float(tap);
    vec4 tapColor = vec4(0.0);
    if (tiledSource != 0) {
      vec2 tapIn;
      vec2 tapSubset;
      vec2 tapClamped;
      vec2 mappedCoord = tiledMapCoord(vec3(sampleCoord, 1.0), false, tiledModeX, tiledModeY,
                                       tiledSubset, tiledClamp, tiledDimension, tapIn, tapSubset,
                                       tapClamped);
      tapColor = texture(sourceSampler, mappedCoord);
      if (seamBlend != 0) {
        tapColor = tiledSeamBlend(sourceSampler, tapColor, tiledModeX, tiledModeY, tiledClamp,
                                  tiledDimension, tapIn, tapSubset, tapClamped, 0);
      }
      if (borderPass != 0) {
        tapColor = tiledApplyBorder(tapColor, tiledModeX, tiledModeY, tiledSubset, tapIn, tapSubset,
                                    tapClamped);
      }
    } else {
      if (subsetEnabled != 0) {
        sampleCoord = clamp(sampleCoord, sourceSubset.xy, sourceSubset.zw);
      }
      tapColor = texture(sourceSampler, sampleCoord);
    }
    sum += tapColor * weight;
    if (tap == tapRadius) {
      break;
    }
  }
  return sum;
}


float tgfxBlurWeight(vec4[17] kernel, int offset) {
  int kernelOffset = abs(offset);
  int kernelSlot = kernelOffset / 4;
  vec4 kernelValues = kernel[0];
  if (kernelSlot == 1) { kernelValues = kernel[1]; }
  if (kernelSlot == 2) { kernelValues = kernel[2]; }
  if (kernelSlot == 3) { kernelValues = kernel[3]; }
  if (kernelSlot == 4) { kernelValues = kernel[4]; }
  if (kernelSlot == 5) { kernelValues = kernel[5]; }
  if (kernelSlot == 6) { kernelValues = kernel[6]; }
  if (kernelSlot == 7) { kernelValues = kernel[7]; }
  if (kernelSlot == 8) { kernelValues = kernel[8]; }
  if (kernelSlot == 9) { kernelValues = kernel[9]; }
  if (kernelSlot == 10) { kernelValues = kernel[10]; }
  if (kernelSlot == 11) { kernelValues = kernel[11]; }
  if (kernelSlot == 12) { kernelValues = kernel[12]; }
  if (kernelSlot == 13) { kernelValues = kernel[13]; }
  if (kernelSlot == 14) { kernelValues = kernel[14]; }
  if (kernelSlot == 15) { kernelValues = kernel[15]; }
  if (kernelSlot == 16) { kernelValues = kernel[16]; }
  int kernelLane = kernelOffset - kernelSlot * 4;
  float weight = kernelValues.x;
  if (kernelLane == 1) { weight = kernelValues.y; }
  if (kernelLane == 2) { weight = kernelValues.z; }
  if (kernelLane == 3) { weight = kernelValues.w; }
  return weight;
}


vec4 tgfxTiledAxisCoord(highp float coord, highp float lo, highp float hi, int mode) {
  if (mode == 0) {
    return vec4(coord, 0.0, 0.0, 0.0);
  }
  highp float w = hi - lo;
  highp float d = coord - lo;
  if (mode == 1) {
        return vec4(mod(d, w) + lo, 0.0, 0.0, 0.0);
  }
    highp float w2 = 2.0 * w;
  highp float m = mod(d, w2);
  highp float o = mix(m, w2 - m, step(w, m));
  if (mode == 2) {
    highp float hw = w / 2.0;
    highp float n = mod(d - hw, w2);
    highp float weight = clamp(mix(n, w2 - n, step(w, n)) - hw + 0.5, 0.0, 1.0);
    return vec4(o + lo, w - o + lo, weight, 0.0);
  }
  return vec4(o + lo, 0.0, 0.0, 0.0);
}


float tgfxTiledAxisClamp(highp float coord, highp float lo, highp float hi, int on) {
  if (on != 0) {
    return clamp(coord, lo, hi);
  }
  return coord;
}


vec4 tgfxTiledSample1(sampler2D tex, highp vec2 coord) {
  return texture(tex, coord);
}


vec4 tgfxTiledBorderLinear(highp vec4 color, highp float errX, highp float errY, int onX,
                           int onY) {
  if (onX != 0) {
    color = mix(color, vec4(0.0), min(abs(errX), 1.0));
  }
  if (onY != 0) {
    color = mix(color, vec4(0.0), min(abs(errY), 1.0));
  }
  return color;
}


void main() {
    vec4 outputColor_P0;
    vec4 outputCoverage_P0;
    { // Processor0 : QuadPerEdgeAAGeometryProcessor
        outputCoverage_P0 = vec4(1.0);
        outputColor_P0 = Color_P0;
    }
    vec4 output_P1;
    { // Processor1 : GaussianBlur1DFragmentProcessor
        vec2 offset = Step_P1;
        int radius = Radius_P1;
        vec4 sum = vec4(0.0);
        for (int j = 0; j <= 40; ++j) {
            int i = j - radius;
        if (i > radius) { break; }
            float weight = tgfxBlurWeight(Kernel_P1, i);
            vec4 tempColor_P1;
            {
                // Processor2 : TiledTextureEffect
                highp vec2 inCoord = (TransformedCoords_0_P0 + offset * float(i));
                inCoord /= Dimension_P2;
                highp vec4 axisX = tgfxTiledAxisCoord(inCoord.x, vec4(0.0).x, vec4(0.0).z, 0);
                highp vec4 axisY = tgfxTiledAxisCoord(inCoord.y, vec4(0.0).y, vec4(0.0).w, 0);
                highp vec2 subsetCoord = vec2(axisX.x, axisY.x);
                highp vec2 clampedCoord = vec2(tgfxTiledAxisClamp(axisX.x, Clamp_P2.x, Clamp_P2.z, 1), tgfxTiledAxisClamp(axisY.x, Clamp_P2.y, Clamp_P2.w, 1));
                vec4 textureColor;
                textureColor = tgfxTiledSample1(TextureSampler_0_P1, (clampedCoord) * Dimension_P2);
                highp float errX = subsetCoord.x - clampedCoord.x;
                highp float errY = subsetCoord.y - clampedCoord.y;
                textureColor = tgfxTiledBorderLinear(textureColor, errX, errY, 1, 1);
                tempColor_P1 = textureColor;
                tempColor_P1 = tempColor_P1 * vec4(1.0).a;
            }
            sum += tempColor_P1 * weight;
        }
        output_P1 = sum;
    }
    { // Processor3 : EmptyXferProcessor
        tgfx_FragColor = output_P1 * outputCoverage_P0;
    }
}

