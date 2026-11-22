//@function transferTF
vec3 transferTF(vec3 x, vec4 tf0, vec4 tf1, int tfType) {
  vec3 s = sign(x);
  x = abs(x);
  float A = tf0[1], B = tf0[2], C = tf0[3];
  float D = tf1[0], E = tf1[1], F = tf1[2];
  if (tfType == 0) {
    float G = tf0[0];
    x = mix(pow(A * x + B, vec3(G)) + E, C * x + F, lessThan(x, vec3(D)));
  } else if (tfType == 1) {
    vec3 xC = pow(x, vec3(C));
    x = pow(max(A + B * xC, 0.0) / (D + E * xC), vec3(F));
  } else if (tfType == 2) {
    x = mix(exp((x - E) * C) + D, pow(x * A, vec3(B)), lessThanEqual(x * A, vec3(1.0)));
    x *= (F + 1.0);
  } else {
    x /= (F + 1.0);
    x = mix(C * log(x - D) + E, A * pow(x, vec3(B)), lessThanEqual(x, vec3(1.0)));
  }
  return s * x;
}

