// Pure pointwise fragment: premultiplied color in, premultiplied color out. No uniforms, no
// samplers, no globals, no specialization surface — the entry and the interpreter both call it.
vec4 pmApplyColorMatrix(vec4 color, mat4 matrix, vec4 vector) {
  color = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
  color = matrix * color + vector;
  color = clamp(color, 0.0, 1.0);
  color.rgb *= color.a;
  return color;
}
