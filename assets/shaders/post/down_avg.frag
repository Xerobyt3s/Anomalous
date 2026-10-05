#version 460 core

uniform sampler2D uSrc;
uniform int uK;
uniform ivec2 uOrigin;
uniform int uFilter;

out vec4 fragColor;

void main() {
    ivec2 dst = ivec2(gl_FragCoord.xy);
    ivec2 base = dst * uK - uOrigin;
    ivec2 sz = textureSize(uSrc, 0);
    if (uFilter == 0 || uK == 1) {
        fragColor = vec4(texelFetch(uSrc, clamp(base + ivec2(uK / 2), ivec2(0), sz - 1), 0).rgb, 1.0);
    } else if (uFilter == 1) {
        ivec2 lo = base + ivec2(max(uK / 2 - 1, 0));
        vec3 acc = vec3(0.0);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                acc += texelFetch(uSrc, clamp(lo + ivec2(x, y), ivec2(0), sz - 1), 0).rgb;
        fragColor = vec4(acc * 0.25, 1.0);
    } else {
        vec3 acc = vec3(0.0);
        for (int y = 0; y < uK; ++y)
            for (int x = 0; x < uK; ++x)
                acc += texelFetch(uSrc, clamp(base + ivec2(x, y), ivec2(0), sz - 1), 0).rgb;
        fragColor = vec4(acc / float(uK * uK), 1.0);
    }
}
