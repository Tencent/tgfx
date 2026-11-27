// The UDF geometry kernel: the fine mask's packed height (24-bit RGB decode), the finite
// gradient through two offset taps, the optional edge-light field (four-tap span gradient and
// band falloff), and the refraction direction blend with the center pull. The body is the text
// route's emission, translated line for line; the edge-light enable arrives as an int constant
// so its branch folds in the driver.
//@function tgxxGlassUDFGeometry
//@op GlassUDFGeometry
//@frag
vec4 tgxxGlassUDFGeometry(sampler2D maskSampler, sampler2D edgeSampler, vec4 inputColor /*@in*/,
                          vec4 GlassShapeP0, vec4 GlassShapeP1, vec4 GlassFineMaskUV,
                          vec2 GlassEdgeSpan, vec4 GlassEdgeMaskUV, float GlassGradientRadius,
                          int edgeOn /*@in*/) {
  vec2 glassUV = inputColor.xy;
  glassUV = vec2(glassUV.x, 1.0 - glassUV.y);
  float halfW = GlassShapeP0.x;
  float halfH = GlassShapeP0.y;
  vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);
  float px = glassPixel.x - halfW;
  float py = glassPixel.y - halfH;
  // The fine mask packs the refraction height into RGB with 24-bit precision; the edge mask
  // carries the edge light height in A.
  const vec3 UNPACK24 = vec3(1.0, 1.0/255.0, 1.0/65025.0);
  vec2 maskUV = vec2(glassUV.x, 1.0 - glassUV.y);
  vec2 fineUV = maskUV * GlassFineMaskUV.xy + GlassFineMaskUV.zw;
  vec4 packedCenter = texture(maskSampler, fineUV);
  float height = dot(packedCenter.rgb, UNPACK24);
  vec2 gradientStep = vec2(max(GlassGradientRadius, 0.0001));
  vec2 gradientUVStep = gradientStep * vec2(0.5 / halfW, 0.5 / halfH) * GlassFineMaskUV.xy;
  vec4 packedRight = texture(maskSampler, fineUV + vec2(gradientUVStep.x, 0.0));
  vec4 packedUp = texture(maskSampler, fineUV - vec2(0.0, gradientUVStep.y));
  float deltaRight = dot(packedRight.rgb, UNPACK24) - height;
  float deltaUp = dot(packedUp.rgb, UNPACK24) - height;
  vec2 gradient = vec2(deltaRight, deltaUp) / gradientStep;
  float gradientLength = length(gradient);
  float gradientSignal = max(abs(deltaRight), abs(deltaUp));
  float gradientWeight = smoothstep(0.001, 0.005, gradientSignal);
  float edgeWeight = 0.0;
  if (edgeOn != 0) {
    vec2 edgeUV = maskUV * GlassEdgeMaskUV.xy + GlassEdgeMaskUV.zw;
    float edgeHeight = texture(edgeSampler, edgeUV).a;
    // Sample half a span to each side so the center difference spans exactly the tent radius
    // that produced the edge field; dividing the height difference by that same span makes the
    // reconstructed distance independent of the radius.
    vec2 edgeUVStep = GlassEdgeSpan * vec2(0.25 / max(halfW, 0.0001), 0.25 / max(halfH, 0.0001)) *
                      GlassEdgeMaskUV.xy;
    vec4 packedEdgeRight = texture(edgeSampler, edgeUV + vec2(edgeUVStep.x, 0.0));
    vec4 packedEdgeLeft = texture(edgeSampler, edgeUV - vec2(edgeUVStep.x, 0.0));
    vec4 packedEdgeUp = texture(edgeSampler, edgeUV - vec2(0.0, edgeUVStep.y));
    vec4 packedEdgeDown = texture(edgeSampler, edgeUV + vec2(0.0, edgeUVStep.y));
    vec2 edgeGradient = vec2((packedEdgeRight.a - packedEdgeLeft.a) / max(GlassEdgeSpan.x, 0.0001),
                             (packedEdgeUp.a - packedEdgeDown.a) / max(GlassEdgeSpan.y, 0.0001));
    float edgeGradientLength = max(length(edgeGradient), 0.0001);
    float edgeDistance = max((edgeHeight - 0.5) / edgeGradientLength, 0.0);
    // The distance is in layer pixels, so the band is widened by the caller when the layer renders
    // at a reduced scale; without that the falloff collapses into a hard threshold.
    float edgeBand = max(1.0, GlassShapeP1.w);
    edgeWeight = 1.0 - smoothstep(0.0, edgeBand, edgeDistance);
  }
  vec4 outputColor = vec4(0.0);
  if (gradientLength > 0.000001 && gradientWeight > 0.000001) {
    vec2 gradientDir = gradient / gradientLength;
    float centerDistance = length(vec2(px, py));
    vec2 centerDir = centerDistance > 0.001 ? -vec2(px, py) / centerDistance : gradientDir;
    vec2 refractDir = mix(gradientDir, centerDir, GlassShapeP1.y);
    float refractLength = length(refractDir);
    refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;
    float normalizedThickness = clamp(GlassShapeP1.z / max(min(halfW, halfH), 0.0001), 0.0, 1.0);
    // This UDF-only shallow-depth curve is intentional and scale-invariant. An external K scales
    // both glassThickness and minHalf, leaving normalizedThickness and therefore depthScale
    // unchanged. The SDF path does not use this profile.
    float depthScale = smoothstep(0.0, 0.1, normalizedThickness);
    float distance = GlassShapeP1.z * GlassShapeP1.x * depthScale * gradientWeight;
    // Proximity can raise the profile above the physical bound; the refraction pass clamps the
    // final displacement to maxDisplacement, which also sizes the background-filter outset.
    float proximity = (1.0 - height * height) * (1.0 - height * height) * 1.2;
    outputColor = vec4(refractDir, distance * proximity, edgeWeight);
  }
  return outputColor;
}
