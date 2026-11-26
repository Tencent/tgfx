// The refraction kernel: the displacement math (offset distance, directional clamp, uv offset),
// the source sampling (single read or chromatic split), the directional edge light and the final
// color assembly. The body is the text route's emission, translated line for line; the
// compile-time variant flags arrive as int constants so the branches fold in the driver and the
// per-variant call arguments stay the only thing that distinguishes the programs.
//@function tgxxGlassRefraction
//@op GlassRefraction
//@frag
vec4 tgxxGlassRefraction(sampler2D sourceSampler, highp vec2 texCoord /*@in*/,
                         vec4 geometry /*@in*/, vec4 GlassOpticsP0, vec4 GlassOpticsP1,
                         vec4 GlassOpticsP2, vec4 GlassOpticsP3, int udfShape /*@in*/,
                         int dispersionOn /*@in*/, int lightingOn /*@in*/) {
  vec2 sourceUV = texCoord * GlassOpticsP0.xy;
  vec2 refractDir = geometry.xy;
  float edgeWeight = geometry.w;
  float offsetDist;
  vec2 displacement;
  if (udfShape != 0) {
    offsetDist = clamp(geometry.z, 0.0, GlassOpticsP0.w);
    displacement = refractDir * offsetDist;
  } else {
    offsetDist = geometry.z;
    displacement = refractDir * offsetDist;
    displacement = clamp(displacement, vec2(-GlassOpticsP0.w), vec2(GlassOpticsP0.w));
  }
  vec2 uvOffset = vec2(displacement.x * GlassOpticsP1.x, -displacement.y * GlassOpticsP1.y);
  vec3 finalColor;
  float srcAlpha;
  if (dispersionOn != 0) {
    vec2 uvR = sourceUV + uvOffset * (1.0 + GlassOpticsP0.z);
    vec2 uvG = sourceUV + uvOffset;
    vec2 uvB = sourceUV + uvOffset * (1.0 - GlassOpticsP0.z);
    vec4 srcG = texture(sourceSampler, uvG);
    finalColor.r = texture(sourceSampler, uvR).r;
    finalColor.g = srcG.g;
    finalColor.b = texture(sourceSampler, uvB).b;
    srcAlpha = srcG.a;
  } else {
    vec4 srcColor = texture(sourceSampler, sourceUV + uvOffset);
    finalColor = srcColor.rgb;
    srcAlpha = srcColor.a;
  }
  if (lightingOn != 0) {
    if (edgeWeight > 0.0) {
      float NdotL = dot(-refractDir, GlassOpticsP1.zw);
      float diffuse = smoothstep(0.35, 1.0, NdotL) * edgeWeight * GlassOpticsP2.z;
      float rim = smoothstep(0.35, 1.0, -NdotL) * edgeWeight * GlassOpticsP2.z * 0.6;
      finalColor += vec3(diffuse + rim);
    }
  }
  return vec4(finalColor, srcAlpha);
}
