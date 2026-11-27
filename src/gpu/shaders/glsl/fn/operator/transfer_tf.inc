//@function transferTF
vec3 transferTF(vec3 x, vec4 tf0, vec4 tf1, int tfType) {
  vec3 s = sign(x);
  vec3 v = abs(x);
  float A = tf0[1], B = tf0[2], C = tf0[3];
  float D = tf1[0], E = tf1[1], F = tf1[2];
  if (tfType == 0) {
    float G = tf0[0];
    v = mix(pow(A * v + B, vec3(G)) + E, C * v + F, lessThan(v, vec3(D)));
  } else if (tfType == 1) {
    vec3 xC = pow(v, vec3(C));
    v = pow(max(A + B * xC, 0.0) / (D + E * xC), vec3(F));
  } else if (tfType == 2) {
    v = mix(exp((v - E) * C) + D, pow(v * A, vec3(B)), lessThanEqual(v * A, vec3(1.0)));
    v *= (F + 1.0);
  } else {
    v /= (F + 1.0);
    v = mix(C * log(v - D) + E, A * pow(v, vec3(B)), lessThanEqual(v, vec3(1.0)));
  }
  return s * v;
}

