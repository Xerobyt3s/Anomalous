// Shared helpers for every shader in the project.
// Included via the directive handled in src/render/shader.cpp.

#ifndef ANOM_COMMON_GLSL
#define ANOM_COMMON_GLSL

const float PI = 3.14159265359;
const float TAU = 6.28318530718;
const float INV_PI = 0.31830988618;

// ---------------------------------------------------------------- hashing

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    return fract(p * (p + p));
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 hash33(vec3 p3) {
    p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

// ---------------------------------------------------------------- gradient noise

vec2 cellGradient(vec2 cell) {
    float angle = hash12(cell) * TAU;
    return vec2(cos(angle), sin(angle));
}

float gradientNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float a = dot(cellGradient(i), f);
    float b = dot(cellGradient(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
    float c = dot(cellGradient(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
    float d = dot(cellGradient(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm2(vec2 p, int octaves) {
    float sum = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < octaves; ++i) {
        sum += gradientNoise(p) * amplitude;
        p *= 2.03;
        amplitude *= 0.5;
    }
    return sum;
}

// ---------------------------------------------------------------- colour

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

// ---------------------------------------------------------------- dithering
//
// Classic 8x8 ordered (Bayer) matrix. Applied in display space just before
// quantisation - this is what turns 6-bit banding into the retro texture we want.

const int kBayer8[64] = int[64](
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21);

// Returns a value in (-0.5, 0.5).
float bayerOffset(ivec2 pixel) {
    int index = (pixel.y & 7) * 8 + (pixel.x & 7);
    return (float(kBayer8[index]) + 0.5) / 64.0 - 0.5;
}

// ---------------------------------------------------------------- phase functions

float henyeyGreenstein(float cosTheta, float g) {
    float gg = g * g;
    float denom = 1.0 + gg - 2.0 * g * cosTheta;
    return (1.0 - gg) / (4.0 * PI * denom * sqrt(max(denom, 1e-4)));
}

float rayleighPhase(float cosTheta) {
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

#endif
