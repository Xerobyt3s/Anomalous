#version 460 core

uniform sampler2D uSrc;
uniform float uThresh;

out vec4 fragColor;

void main() {
    ivec2 base = ivec2(gl_FragCoord.xy) * 4;
    ivec2 sz = textureSize(uSrc, 0);
    vec3 acc = vec3(0.0);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            vec3 c = texelFetch(uSrc, min(base + ivec2(x, y), sz - 1), 0).rgb;

            acc += (any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : max(c, vec3(0.0));
        }
    acc *= 1.0 / 16.0;
    float l = max(acc.r, max(acc.g, acc.b));
    fragColor = vec4(acc * smoothstep(uThresh, 1.0, l), 1.0);
}
