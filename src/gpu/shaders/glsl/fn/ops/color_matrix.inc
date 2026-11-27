//@op ColorMatrix
//@frag
vec4 pmApplyColorMatrix(vec4 color /*@in*/, mat4 matrix, vec4 vector) {
  vec4 result = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
  result = matrix * result + vector;
  result = clamp(result, 0.0, 1.0);
  result.rgb *= result.a;
  return result;
}
