// The tent-blur body: the loop and the field encoding are the legacy kernel's, verbatim.
#define ShaderModeX GlassUDFTentBlur_ShaderModeX
#define ShaderModeY GlassUDFTentBlur_ShaderModeY
#define Subset GlassUDFTentBlur_Subset
#define Clamp GlassUDFTentBlur_Clamp
#define Dimension GlassUDFTentBlur_Dimension
#include "source/tiled_sample.inc"
#undef ShaderModeX
#undef ShaderModeY
#undef Subset
#undef Clamp
#undef Dimension

void GlassUDFTentBlur_fragment(inout Fragment f) {
  vec2 offset = GlassUDFTentBlur_Step;
  float radius = (GlassUDFTentBlur_Field == 1) ? GlassUDFTentBlur_Radius.y : GlassUDFTentBlur_Radius.x;
  const vec3 UNPACK24 = vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0);
  bool decodePacked = (GlassUDFTentBlur_Field == 0) && (GlassUDFTentBlur_InputIsPacked != 0);

  float sum = 0.0;
  float total = 0.0;
  int k = 1;
  float s = 1.0;
  for (int j = 0; j <= 64; ++j) {
    float offsetValue;
    float weight;
    if (j == 0) {
      offsetValue = 0.0;
      weight = radius;
    } else {
      float w1 = max(0.0, radius - float(k));
      float w2 = max(0.0, radius - float(k + 1));
      weight = w1 + w2;
      offsetValue = (weight > 0.0) ? s * (float(k) + w2 / weight) : 0.0;
    }
    if (weight <= 0.0) {
      break;
    }
    total += weight;
    vec2 sampleCoord = BlurSourceGeometry_vCoord + offset * offsetValue;
    vec4 texColor = vec4(0.0);
    if (GlassUDFTentBlur_TiledChild != 0) {
      vec2 tapInCoord;
      vec2 tapSubsetCoord;
      vec2 tapClampedCoord;
      vec2 tapCoord = tiledMapCoord(sampleCoord, false, tapInCoord, tapSubsetCoord, tapClampedCoord);
      texColor = texture(GlassUDFTentBlur_Sampler, tapCoord);
      // RepeatLinearNone(3) seam blend, ported from the runtime GLSLTiledTextureEffect emission and
      // once shared with the Gaussian blur kernel.
      bool repeatX = GlassUDFTentBlur_ShaderModeX == 3 && tapSubsetCoord.x != tapClampedCoord.x;
      bool repeatY = GlassUDFTentBlur_ShaderModeY == 3 && tapSubsetCoord.y != tapClampedCoord.y;
      if (repeatX || repeatY) {
        float errX = tapSubsetCoord.x - tapClampedCoord.x;
        float errY = tapSubsetCoord.y - tapClampedCoord.y;
        float repeatCoordX = errX > 0.0 ? GlassUDFTentBlur_Clamp.x : GlassUDFTentBlur_Clamp.z;
        float repeatCoordY = errY > 0.0 ? GlassUDFTentBlur_Clamp.y : GlassUDFTentBlur_Clamp.w;
        if (repeatX && repeatY) {
          vec4 repeatReadX =
              texture(GlassUDFTentBlur_Sampler, vec2(repeatCoordX, tapClampedCoord.y) * GlassUDFTentBlur_Dimension);
          vec4 repeatReadY =
              texture(GlassUDFTentBlur_Sampler, vec2(tapClampedCoord.x, repeatCoordY) * GlassUDFTentBlur_Dimension);
          vec4 repeatReadXY = texture(GlassUDFTentBlur_Sampler, vec2(repeatCoordX, repeatCoordY) * GlassUDFTentBlur_Dimension);
          texColor = mix(mix(texColor, repeatReadX, abs(errX)),
                         mix(repeatReadY, repeatReadXY, abs(errX)), abs(errY));
        } else if (repeatX) {
          vec4 repeatReadX =
              texture(GlassUDFTentBlur_Sampler, vec2(repeatCoordX, tapClampedCoord.y) * GlassUDFTentBlur_Dimension);
          texColor = mix(texColor, repeatReadX, errX);
        } else {
          vec4 repeatReadY =
              texture(GlassUDFTentBlur_Sampler, vec2(tapClampedCoord.x, repeatCoordY) * GlassUDFTentBlur_Dimension);
          texColor = mix(texColor, repeatReadY, errY);
        }
      }
      texColor = tiledApplyBorder(texColor, tapInCoord, tapSubsetCoord, tapClampedCoord);
    } else {
      sampleCoord = clamp(sampleCoord, GlassUDFTentBlur_Subset.xy, GlassUDFTentBlur_Subset.zw);
      texColor = texture(GlassUDFTentBlur_Sampler, sampleCoord);
    }
    float value = decodePacked ? dot(texColor.rgb, UNPACK24) : texColor.a;
    sum += value * weight;
    if (j > 0) {
      if (s < 0.0) {
        k += 2;
      }
      s = -s;
    }
  }
  sum /= total;

  vec4 fieldColor = vec4(0.0);
  if (GlassUDFTentBlur_Field == 1) {
    fieldColor = vec4(0.0, 0.0, 0.0, clamp(sum, 0.0, 1.0));
  } else {
    // Encode the fine field into RGB with true 24-bit precision; the fract chain pre-compensates
    // the 8-bit rounding of the following channel. The upper clamp is required because fract(1.0)
    // == 0.0 and interior UDF samples are exactly 1.0.
    float fineValue = clamp(sum, 0.0, 1.0 - 1.0 / 16581375.0);
    vec3 enc = fract(vec3(1.0, 255.0, 65025.0) * fineValue);
    enc.x -= enc.y / 255.0;
    enc.y -= enc.z / 255.0;
    fieldColor = vec4(enc, 0.0);
  }
  f.color = fieldColor;
}
