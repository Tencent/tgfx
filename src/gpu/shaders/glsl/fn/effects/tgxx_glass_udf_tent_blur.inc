// The UDF tent-blur kernel: the pairwise-bilinear tap walk (alternating sides, weights falling
// off linearly to the radius), the packed-field decode (RGBA8 dot for the fine field, alpha for
// the coarse one) and the result packing (24-bit fract encode for the fine field, clamped alpha
// for the edge-light one). The body is the text route's emission, translated line for line; the
// compile-time variant flags arrive as int constants so the branches fold in the driver.
//@function tgxxGlassUDFTentBlur
//@op GlassUDFTentBlur
//@frag
vec4 tgxxGlassUDFTentBlur(sampler2D sourceSampler, highp vec2 texCoord /*@in*/, float Radius,
                          vec2 Step, int maxRadius /*@in*/, int refractionPacked /*@in*/,
                          int edgeLight /*@in*/) {
  vec2 offset = Step;
  float radius = Radius;
  float sum = 0.0;
  float total = 0.0;
  // RGBA8 unpack constants: dot(rgba, UNPACK) recovers the original float.
  const vec4 UNPACK = vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0);
  // Pairwise bilinear sampling: combine adjacent positions (k, k+1) into a single texture fetch
  // via hardware bilinear filtering. The tent kernel's linear weights make this exact. The fixed
  // 64-iteration upper bound satisfies GLSL ES 1.0's constant-loop constraint; the actual exit is
  // the weight<=0 break at radius+1 iterations.
  int k = 1;
  float s = 1.0;
  for (int j = 0; j <= maxRadius; ++j) {
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
    if (weight <= 0.0) { break; }
    total += weight;
    vec4 tempColor = texture(sourceSampler, texCoord + offset * offsetValue);
    if (refractionPacked != 0) {
      sum += dot(tempColor, UNPACK) * weight;
    } else {
      sum += tempColor.a * weight;
    }
    if (j > 0) {
      if (s < 0.0) { k += 2; }
      s = -s;
    }
  }
  float result = sum / total;
  if (edgeLight != 0) {
    // The coarse field carries one 8-bit value in alpha.
    return vec4(0.0, 0.0, 0.0, clamp(result, 0.0, 1.0));
  }
  // Encode the fine field into RGB with true 24-bit precision; the fract chain pre-compensates
  // the 8-bit rounding of the following channel. The upper clamp is required because fract(1.0)
  // == 0.0 and interior UDF samples are exactly 1.0.
  float fineValue = clamp(result, 0.0, 1.0 - 1.0 / 16581375.0);
  vec3 enc = fract(vec3(1.0, 255.0, 65025.0) * fineValue);
  enc.x -= enc.y / 255.0;
  enc.y -= enc.z / 255.0;
  return vec4(enc, 0.0);
}
