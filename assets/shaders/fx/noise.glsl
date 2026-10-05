#ifndef GHOST_NOISE_GLSL
#define GHOST_NOISE_GLSL
float n_hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float n_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = n_hash(i), b = n_hash(i + vec2(1, 0)), c = n_hash(i + vec2(0, 1)), d = n_hash(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float n_fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { v += a * n_noise(p); p = p * 2.13 + vec2(11.7, 7.3); a *= 0.5; }
    return v;
}

float n_hash3(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float n_noise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(n_hash3(i), n_hash3(i + vec3(1, 0, 0)), f.x),
                   mix(n_hash3(i + vec3(0, 1, 0)), n_hash3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(n_hash3(i + vec3(0, 0, 1)), n_hash3(i + vec3(1, 0, 1)), f.x),
                   mix(n_hash3(i + vec3(0, 1, 1)), n_hash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float n_fbm3(vec3 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { v += a * n_noise3(p); p = p * 2.07 + vec3(5.3, 1.7, 9.1); a *= 0.5; }
    return v / 0.875;
}

#endif
