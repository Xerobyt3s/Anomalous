#version 460 core

uniform sampler2D uSrc;
uniform ivec2 uDir;

out vec4 fragColor;

const float w[5] = float[5](0.227027, 0.194594, 0.121622, 0.054054, 0.016216);

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 sz = textureSize(uSrc, 0);
    vec3 acc = texelFetch(uSrc, p, 0).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        acc += texelFetch(uSrc, clamp(p + uDir * i, ivec2(0), sz - 1), 0).rgb * w[i];
        acc += texelFetch(uSrc, clamp(p - uDir * i, ivec2(0), sz - 1), 0).rgb * w[i];
    }
    fragColor = vec4(acc, 1.0);
}
