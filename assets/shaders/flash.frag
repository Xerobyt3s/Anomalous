#version 460 core

in vec2 vLocal;

out vec4 fragColor;

uniform vec3 uColor;
uniform float uIntensity;
uniform float uSeed;
uniform float uSpikes;
uniform float uOpacity;

uniform float uRing;
uniform float uDecal;
uniform sampler2D uSceneDepth;
uniform vec2 uNF;
uniform float uSplit;

float viewDepth(float d) {
    float ndc = (d - uSplit) / (1.0 - uSplit) * 2.0 - 1.0;
    return 2.0 * uNF.x * uNF.y / (uNF.y + uNF.x - ndc * (uNF.y - uNF.x));
}

void main() {
    if (uDecal > 0.5) {
        float behind = viewDepth(texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r);
        float here = viewDepth(gl_FragCoord.z);
        if (behind > here + 0.05 + 0.02 * here) discard;
    }
    float r = length(vLocal);
    float a = atan(vLocal.y, vLocal.x);

    if (uOpacity > 0.0) {
        float lumps = 0.12 * sin(a * 3.0 + uSeed) * sin(a * 5.0 + uSeed * 2.3) + 0.06 * sin(a * 9.0 - uSeed);
        float density = 1.0 - smoothstep(0.25, 0.95, r + lumps);
        density *= density;
        float alpha = density * uOpacity;
        fragColor = vec4(uColor * alpha, alpha);
        return;
    }

    if (uRing > 0.5) {
        float wobble = 0.035 * sin(a * 5.0 + uSeed) + 0.025 * sin(a * 9.0 - uSeed * 1.7);
        float d = abs(r - 0.76 + wobble);
        float rim = exp(-d * d * 700.0) + 0.2 * exp(-d * d * 60.0);
        vec3 hot = mix(uColor, vec3(1.0, 0.95, 0.8), clamp(rim - 0.55, 0.0, 1.0));
        fragColor = vec4(hot * rim * uIntensity, 0.0);
        return;
    }

    float spikeShape = pow(abs(cos(a * 2.5 + uSeed)), 10.0) * (0.6 + 0.4 * sin(a * 7.0 + uSeed * 3.1));
    float spikes = spikeShape * max(1.0 - r, 0.0) * uSpikes;
    float core = exp(-r * r * 14.0);
    float glow = exp(-r * 4.0) * 0.35;

    float v = (core + spikes + glow) * uIntensity * (1.0 - smoothstep(0.85, 1.0, r));
    fragColor = vec4(uColor * v, 0.0);
}
