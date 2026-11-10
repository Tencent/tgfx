// GlassRefraction fragment hook (the former kernels/glass_refraction.frag): displaces the
// background sample along the glass shape's refracted direction, adds the edge-light response and
// replaces the running color. The coverage is NOT touched here: the geometry module seeds it
// (the ellipse's analytic edge or the quad's per-vertex coverage) and the local-mask and clip
// modules multiply onto it. The expressions are verbatim from the legacy kernel, modulo the
// module-prefixed uniform names; DISPERSION stays a compile-time branch for the same reason the
// legacy kernel kept it one — keeping both taps in one text lets the compiler hoist the shared
// `sourceUV + uvOffset` across the branch, which shifts the sampled coordinate by an ULP and
// flips the occasional texel.

// The shared angular edge-light response (mirrors EmitFaceLight): the light-facing edge with
// coefficient 1 and the back-side rim with 0.6. Shared by the SDF and UDF paths so retuning the
// coefficients changes both together.
float glassFaceLight(vec2 n, vec2 l) {
  float d = dot(n, l);
  return smoothstep(0.35, 1.0, d) + 0.6 * smoothstep(0.35, 1.0, -d);
}

// The shared edge-band falloff (mirrors EmitEdgeBand); the band floor is max(1.0, ShapeP1.w),
// assigned by the caller before this runs. Shared so the band slope lives in exactly one place.
float glassEdgeBandWeight(float edgeDistance, float band) {
  return smoothstep(-(band + 0.001), -0.2 * band, -edgeDistance);
}

#if GlassRefraction_SHAPE <= 1

// The SDF functions and gradient math mirror GLSLGlassSDFGeometryFragmentProcessor::emitCode
// expression-for-expression. glassUV arrives with the refraction emission's orientation; the
// geometry emission flips it once (EmitGeometryCoordinates).
float glassShapeSDFRoundedRect(float px, float py, float hw, float hh, float r) {
  float dx = abs(px) - hw + r;
  float dy = abs(py) - hh + r;
  float outside = length(max(vec2(dx, dy), vec2(0.0)));
  return outside + min(max(dx, dy), 0.0) - r;
}

float glassShapeSDFEllipse(float px, float py, float hw, float hh) {
  float normalizedDist = length(vec2(px / hw, py / hh));
  float gradientLength = length(vec2(px / (hw * hw), py / (hh * hh)));
  if (gradientLength < 0.000001) {
    return -min(hw, hh);
  }
  return normalizedDist * (normalizedDist - 1.0) / gradientLength;
}

vec4 glassGeometry(vec2 glassUV) {
  glassUV = vec2(glassUV.x, 1.0 - glassUV.y);
  float halfW = GlassRefraction_ShapeP0.x;
  float halfH = GlassRefraction_ShapeP0.y;
  vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);
  float px = glassPixel.x - halfW;
  float py = glassPixel.y - halfH;
  float cornerRadius = GlassRefraction_ShapeP0.z;
#if GlassRefraction_SHAPE == 0
  float outerSDF = glassShapeSDFRoundedRect(px, py, halfW, halfH, cornerRadius);
#else
  float outerSDF = glassShapeSDFEllipse(px, py, halfW, halfH);
#endif
  vec4 result = vec4(0.0);
  if (outerSDF < 0.0) {
    float edgeDist = -outerSDF;
    float rd = max(GlassRefraction_ShapeP0.w, 0.0001);
    float heightT = clamp((rd - edgeDist) / rd, 0.0, 1.0);
    float offsetDist = min(GlassRefraction_ShapeP1.x * rd * pow(heightT, 3.5), 0.999 * rd);
    float effectiveSplay = GlassRefraction_ShapeP1.y;
    // Keep a fallback direction so the analytical edge light still evaluates on degenerate
    // gradient pixels; only the direction computation is gated below, not the light response.
    vec2 refractDir = vec2(0.0);
#if GlassRefraction_SHAPE == 1
    vec2 sdfGradient = vec2(px / (halfW * halfW), py / (halfH * halfH));
    float gradientLength = length(sdfGradient);
    if (gradientLength > 0.000001) {
      vec2 gradientDir = -sdfGradient / gradientLength;
#else
    vec2 absP = abs(vec2(px, py));
    float refractionDistance = GlassRefraction_ShapeP0.w;
    float r_effective = min(min(halfW, halfH), max(cornerRadius, refractionDistance));
    vec2 cornerCenter = vec2(halfW - r_effective, halfH - r_effective);
    vec2 relToCorner = absP - cornerCenter;
    float compensation = max(-min(relToCorner.x, relToCorner.y), 0.0);
    vec2 grad = relToCorner + vec2(compensation);
    float qx = absP.x - cornerCenter.x;
    float qy = absP.y - cornerCenter.y;
    float straightHalfW = max(cornerCenter.x, 0.0001);
    float straightHalfH = max(cornerCenter.y, 0.0001);
    float cornerWeightX = smoothstep(0.0, straightHalfW, absP.x);
    float cornerWeightY = smoothstep(0.0, straightHalfH, absP.y);
    float cornerWeight = (qx > 0.0 && qy > 0.0)
                             ? 1.0
                             : ((qx > qy) ? cornerWeightY : cornerWeightX);
    effectiveSplay = min(cornerWeight + GlassRefraction_ShapeP1.y, 1.0);
    float gradientLength = length(grad);
    if (gradientLength > 0.000001) {
      vec2 gradientDir = -vec2(sign(px) * grad.x, sign(py) * grad.y) / gradientLength;
#endif
      float centerDistance = length(vec2(px, py));
      vec2 centerDir =
          centerDistance > 0.001 ? -vec2(px, py) / centerDistance : gradientDir;
      refractDir = mix(gradientDir, centerDir, effectiveSplay);
      float refractLength = length(refractDir);
      refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;
    }
    // The edge light does not depend on the gradient direction, so it is evaluated outside the
    // gate above: degenerate gradient pixels keep a zero refraction (refractDir stays (0, 0)) but
    // still receive the analytical highlight. darkTerm reads the final refractDir assigned inside
    // the gate. Runtime branch: the SDF emission compiles the flag in (enableEdgeLighting ==
    // lightIntensity > 0), which this module serves as the LightingOn uniform.
    float lightResponse = 0.0;
    if (GlassRefraction_LightingOn != 0) {
      float edgeBand = max(1.0, GlassRefraction_ShapeP1.w);
      float edgeWeight = glassEdgeBandWeight(edgeDist, edgeBand);
      // Shade the two adjacent edge faces separately and blend their responses, rather than
      // shading a single normalized blend of the two face normals (a blended normal can collapse
      // to zero at a corner); a rounded corner keeps its exact radial normal.
#if GlassRefraction_SHAPE == 0
      float lightCornerRadius = min(min(halfW, halfH), cornerRadius);
      float lightQx = abs(px) - (halfW - lightCornerRadius);
      float lightQy = abs(py) - (halfH - lightCornerRadius);
      // The corner transition spans one bevel width, a fraction of the edge band.
      float faceMix = smoothstep(-0.5 * edgeBand, 0.5 * edgeBand, lightQx - lightQy);
      float faceResponse =
          faceMix * glassFaceLight(vec2(sign(px), 0.0), GlassRefraction_LightDir.xy) +
          (1.0 - faceMix) * glassFaceLight(vec2(0.0, sign(py)), GlassRefraction_LightDir.xy);
      vec2 arcVec = sign(vec2(px, py)) * vec2(lightQx, lightQy);
      float arcLen = length(arcVec);
      float arcResponse =
          arcLen > 0.000001 ? glassFaceLight(arcVec / arcLen, GlassRefraction_LightDir.xy) : 0.0;
      // The arc response applies only where both q are positive; its weight fades to zero where
      // the arc normal meets the matching edge-face normal, keeping the branches continuous.
      float arcWeight = smoothstep(0.0, 0.5 * edgeBand, lightQx) *
                        smoothstep(0.0, 0.5 * edgeBand, lightQy);
      float faceLightValue = mix(faceResponse, arcResponse, arcWeight);
#else
      vec2 ellipseNormal = vec2(px / (halfW * halfW), py / (halfH * halfH));
      float ellipseLen = length(ellipseNormal);
      float faceLightValue = ellipseLen > 0.000001
                                 ? glassFaceLight(ellipseNormal / ellipseLen,
                                                  GlassRefraction_LightDir.xy)
                                 : 0.0;
#endif
      float edgeLight = edgeWeight * faceLightValue;
      float bandRamp = heightT * heightT;
      float darkTerm = dot(refractDir, GlassRefraction_LightDir.xy) * bandRamp * 0.05;
      lightResponse = edgeLight + darkTerm;
    }
    result = vec4(refractDir, offsetDist, lightResponse);
  }
  return result;
}

#else

// The UDF gradient reconstruction mirrors GLSLGlassUDFGeometryFragmentProcessor::emitCode
// expression-for-expression: the fine mask packs the refraction height into RGB, the edge mask
// carries the edge light height in A, and the edge center difference spans the tent radius that
// produced the edge field.
vec4 glassGeometry(vec2 glassUV) {
  glassUV = vec2(glassUV.x, 1.0 - glassUV.y);
  float halfW = GlassRefraction_ShapeP0.x;
  float halfH = GlassRefraction_ShapeP0.y;
  vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);
  float px = glassPixel.x - halfW;
  float py = glassPixel.y - halfH;
  const vec3 UNPACK24 = vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0);
  vec2 maskUV = vec2(glassUV.x, 1.0 - glassUV.y);
  vec2 fineUV = maskUV * GlassRefraction_FineMaskUV.xy + GlassRefraction_FineMaskUV.zw;
  vec4 packedCenter = texture(GlassRefraction_MaskSampler, fineUV);
  float height = dot(packedCenter.rgb, UNPACK24);
  float gradientBase = GlassRefraction_ShapeP1.z * 3.0 + 1.0;
  vec2 gradientStep = gradientBase * GlassRefraction_ShapeP0.zw;
  vec2 gradientUVStep =
      gradientStep * vec2(0.5 / halfW, 0.5 / halfH) * GlassRefraction_FineMaskUV.xy;
  vec4 packedRight = texture(GlassRefraction_MaskSampler, fineUV + vec2(gradientUVStep.x, 0.0));
  vec4 packedUp = texture(GlassRefraction_MaskSampler, fineUV - vec2(0.0, gradientUVStep.y));
  float deltaRight = dot(packedRight.rgb, UNPACK24) - height;
  float deltaUp = dot(packedUp.rgb, UNPACK24) - height;
  vec2 gradient = vec2(deltaRight, deltaUp) / gradientStep;
  float gradientLength = length(gradient);
  float gradientSignal = max(abs(deltaRight), abs(deltaUp));
  float gradientWeight = smoothstep(0.001, 0.005, gradientSignal);
  float edgeWeight = 0.0;
  // Runtime branch (see EdgeMaskOn in the uniform block): an edge-lit-off UDF draw never executes
  // the mask taps, so the dummy-bound EdgeMaskSampler content stays unsampled and the result is
  // bit-identical to the previous separate no-edge variant.
  if (GlassRefraction_EdgeMaskOn != 0) {
    vec2 edgeUV = maskUV * GlassRefraction_EdgeMaskUV.xy + GlassRefraction_EdgeMaskUV.zw;
    float edgeHeight = texture(GlassRefraction_EdgeMaskSampler, edgeUV).a;
    vec2 edgeUVStep = GlassRefraction_EdgeSpan *
                      vec2(0.25 / max(halfW, 0.0001), 0.25 / max(halfH, 0.0001)) *
                      GlassRefraction_EdgeMaskUV.xy;
    vec4 packedEdgeRight =
        texture(GlassRefraction_EdgeMaskSampler, edgeUV + vec2(edgeUVStep.x, 0.0));
    vec4 packedEdgeLeft =
        texture(GlassRefraction_EdgeMaskSampler, edgeUV - vec2(edgeUVStep.x, 0.0));
    vec4 packedEdgeUp = texture(GlassRefraction_EdgeMaskSampler, edgeUV - vec2(0.0, edgeUVStep.y));
    vec4 packedEdgeDown = texture(GlassRefraction_EdgeMaskSampler, edgeUV + vec2(0.0, edgeUVStep.y));
    vec2 edgeGradient =
        vec2((packedEdgeRight.a - packedEdgeLeft.a) / max(GlassRefraction_EdgeSpan.x, 0.0001),
             (packedEdgeUp.a - packedEdgeDown.a) / max(GlassRefraction_EdgeSpan.y, 0.0001));
    float edgeGradientLength = max(length(edgeGradient), 0.0001);
    float edgeDistance = max((edgeHeight - 0.5) / edgeGradientLength, 0.0);
    // The distance is in layer pixels, so the band is widened by the caller when the layer
    // renders at a reduced scale (mirrors EmitEdgeBand's floor).
    float edgeBand = max(1.0, GlassRefraction_ShapeP1.w);
    edgeWeight = glassEdgeBandWeight(edgeDistance, edgeBand);
  }
  vec4 result = vec4(0.0);
  if (gradientLength > 0.000001 && gradientWeight > 0.000001) {
    vec2 gradientDir = gradient / gradientLength;
    float centerDistance = length(vec2(px, py));
    vec2 centerDir = centerDistance > 0.001 ? -vec2(px, py) / centerDistance : gradientDir;
    vec2 refractDir = mix(gradientDir, centerDir, GlassRefraction_ShapeP1.y);
    float refractLength = length(refractDir);
    refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;
    float depthScale = smoothstep(0.0, 0.1, GlassRefraction_ShapeP1.z);
    float distance = min(halfW, halfH) * GlassRefraction_ShapeP1.x * GlassRefraction_ShapeP1.z *
                     depthScale * gradientWeight;
    float proximity = (1.0 - height * height) * (1.0 - height * height) * 1.2;
    // Same angular response as the analytical path, evaluated on the post-splay normalized
    // refractDir instead of an analytical face normal. EdgeMaskOn gates the edge field above, so
    // an edge-lit-off draw keeps edgeWeight 0 and edgeLight stays 0 with no extra branch here.
    float edgeLight = edgeWeight * glassFaceLight(-refractDir, GlassRefraction_LightDir.xy);
    result = vec4(refractDir, distance * proximity, edgeLight);
  }
  return result;
}

#endif

void GlassRefraction_fragment(inout Fragment f) {
  // The refraction body mirrors GLSLGlassRefractionFragmentProcessor::emitCode
  // expression-for-expression; the static dispersion/lighting branches of the runtime emission
  // ride the DISPERSION dim and the LightingOn uniform here with identical math. The color
  // replaces the geometry's (the refraction output is the drawn color); the coverage stays
  // whatever the geometry seeded.
  vec2 backgroundUV = GlassRefraction_vCoord;
  vec2 sourceUV = backgroundUV * GlassRefraction_OpticsP0.xy;
  vec2 glassUV = backgroundUV * GlassRefraction_OpticsP2.xy + GlassRefraction_OpticsP3.xy;

  vec4 geometry = glassGeometry(glassUV);
  vec2 refractDir = geometry.xy;
  float offsetDist = geometry.z;
  float lightResponse = geometry.w;
  vec2 displacement = refractDir * offsetDist;
  displacement =
      clamp(displacement, vec2(-GlassRefraction_OpticsP0.w), vec2(GlassRefraction_OpticsP0.w));
  vec2 uvOffset =
      vec2(displacement.x * GlassRefraction_OpticsP1.x, -displacement.y * GlassRefraction_OpticsP1.y);

  vec3 finalColor;
  float srcAlpha;
  // Compile-time, not a runtime branch: keeping both taps in one text lets the compiler hoist the
  // shared `sourceUV + uvOffset` across the branch, which shifts the sampled coordinate by an ULP
  // against the runtime's branch-free program and flips the occasional texel. The dim reproduces
  // the runtime emission's structure exactly — it emits one path or the other and never both.
#if GlassRefraction_DISPERSION
  vec2 uvR = sourceUV + uvOffset * (1.0 + GlassRefraction_OpticsP0.z);
  vec2 uvG = sourceUV + uvOffset;
  vec2 uvB = sourceUV + uvOffset * (1.0 - GlassRefraction_OpticsP0.z);
  vec4 srcG = texture(GlassRefraction_Sampler, uvG);
  finalColor.r = texture(GlassRefraction_Sampler, uvR).r;
  finalColor.g = srcG.g;
  finalColor.b = texture(GlassRefraction_Sampler, uvB).b;
  srcAlpha = srcG.a;
#else
  vec4 srcColor = texture(GlassRefraction_Sampler, sourceUV + uvOffset);
  finalColor = srcColor.rgb;
  srcAlpha = srcColor.a;
#endif

  if (GlassRefraction_LightingOn != 0) {
    finalColor += vec3(lightResponse * GlassRefraction_OpticsP2.z);
  }
  f.color = vec4(finalColor, srcAlpha);
}
