#version 460 core

const int kMaxLights = 8;

in vec3 vWorld;
in vec3 vNormal;
out vec4 fragColor;

uniform vec3 uCameraPos;
uniform vec3 uLightDir;
uniform vec3 uSunColor;
uniform int uPointCount;
uniform vec3 uPointPos[kMaxLights];
uniform vec3 uPointColor[kMaxLights];

void main() {
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCameraPos - vWorld);
    if (dot(n, v) < 0.0) n = -n;
    float facing = max(dot(n, v), 0.0);
    float fresnel = 0.05 + 0.95 * pow(1.0 - facing, 5.0);
    vec3 r = reflect(-v, n);

    vec3 sky = mix(vec3(0.01, 0.011, 0.014), vec3(0.16, 0.18, 0.22), clamp(r.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 color = vec3(0.012, 0.012, 0.016) * (0.4 + 0.6 * max(dot(n, uLightDir), 0.0)) + sky * fresnel;
    color += uSunColor * pow(max(dot(r, uLightDir), 0.0), 120.0) * 0.5;

    for (int i = 0; i < min(uPointCount, kMaxLights); ++i) {
        vec3 to = uPointPos[i] - vWorld;
        float d2 = max(dot(to, to), 0.05);
        vec3 l = to * inversesqrt(d2);
        color += uPointColor[i] / d2 * (pow(max(dot(r, l), 0.0), 60.0) * 1.2 + max(dot(n, l), 0.0) * 0.03);
    }

    color += vec3(0.35, 0.45, 0.6) * pow(1.0 - facing, 3.0) * 0.18;
    if (any(isnan(color)) || any(isinf(color))) color = vec3(0.0);
    fragColor = vec4(color, 1.0);
}
