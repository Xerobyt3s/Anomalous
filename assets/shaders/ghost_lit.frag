#version 460 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
in vec3 vObjPos;
in vec3 vObjNormal;

out vec4 fragColor;

uniform vec3 uCameraPos;
uniform vec3 uLightDir;
uniform vec3 uSunColor;
const int kMaxLights = 8;
uniform int uPointCount;
uniform vec3 uPointPos[kMaxLights];
uniform vec3 uPointColor[kMaxLights];
uniform vec3 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uHighlight;
uniform vec3 uEmissive;

uniform float uGhost;
uniform float uGhostTime;

uniform int uUseMaps;
uniform sampler2D uColorMap;
uniform sampler2D uNormalMap;
uniform sampler2D uRoughMap;
uniform float uMapScale;
uniform vec3 uMapTint;
uniform float uMapRoughScale;
uniform mat3 uMapNormalMatrix;
uniform float uNormalStrength;
uniform float uMapContrast;
uniform float uBlendSharpness;
uniform float uGridOverlay;

uniform vec3 uSootFrom;
uniform vec3 uSootTo;
uniform float uSoot;

vec3 gBase;
float gRough;
float gGrazing;

const float PI = 3.14159265;

vec3 environment(vec3 dir) {
    return mix(vec3(0.05, 0.045, 0.04), vec3(0.35, 0.40, 0.50), clamp(dir.y * 0.5 + 0.5, 0.0, 1.0));
}

vec3 shade(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 F0) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float alpha = gRough * gRough;
    float alpha2 = alpha * alpha;
    float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    float D = alpha2 / (PI * denom * denom);
    float k = (gRough + 1.0) * (gRough + 1.0) / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    vec3 specular = D * G * F / (4.0 * NdotV * NdotL + 1e-4);
    vec3 diffuse = (1.0 - F) * (1.0 - uMetallic) * gBase / PI;
    return (diffuse + specular) * radiance * NdotL;
}

void applyMaps(inout vec3 N) {
    vec3 n = normalize(vObjNormal + vec3(1e-6));
    vec3 w = pow(abs(n), vec3(uBlendSharpness));
    w /= max(w.x + w.y + w.z, 1e-5);
    vec3 p = vObjPos * uMapScale;
    vec2 uvX = p.zy;
    vec2 uvY = p.xz;
    vec2 uvZ = p.xy;
    vec3 color = texture(uColorMap, uvX).rgb * w.x + texture(uColorMap, uvY).rgb * w.y + texture(uColorMap, uvZ).rgb * w.z;
    float rough = texture(uRoughMap, uvX).r * w.x + texture(uRoughMap, uvY).r * w.y + texture(uRoughMap, uvZ).r * w.z;
    vec3 tX = (texture(uNormalMap, uvX).xyz * 2.0 - 1.0) * vec3(uNormalStrength, uNormalStrength, 1.0);
    vec3 tY = (texture(uNormalMap, uvY).xyz * 2.0 - 1.0) * vec3(uNormalStrength, uNormalStrength, 1.0);
    vec3 tZ = (texture(uNormalMap, uvZ).xyz * 2.0 - 1.0) * vec3(uNormalStrength, uNormalStrength, 1.0);
    tX = vec3(tX.xy + n.zy, abs(tX.z) * n.x);
    tY = vec3(tY.xy + n.xz, abs(tY.z) * n.y);
    tZ = vec3(tZ.xy + n.xy, abs(tZ.z) * n.z);
    vec3 objN = normalize(tX.zyx * w.x + tY.xzy * w.y + tZ.xyz * w.z + vec3(1e-6));
    vec3 worldN = uMapNormalMatrix * objN;
    float len = length(worldN);
    if (len > 1e-6) {
        N = worldN / len;
    }
    vec3 average = textureLod(uColorMap, vec2(0.5), 16.0).rgb;
    gBase = mix(average, color, uMapContrast) * uMapTint;

    gGrazing = 0.35;
    if (uSoot > 0.0) {
        vec3 axis = uSootTo - uSootFrom;
        float along = clamp(dot(vObjPos - uSootFrom, axis) / max(dot(axis, axis), 1e-8), 0.0, 1.0);
        float soot = uSoot * smoothstep(0.15, 1.0, along);
        gBase *= 1.0 - 0.75 * soot;
        rough = mix(rough, 1.0, 0.5 * soot);
        gGrazing *= 1.0 - 0.8 * soot;
    }
    gRough = clamp(rough * uMapRoughScale, 0.04, 1.0);
}

float gridLine(vec2 p, float spacing) {
    vec2 cell = p / spacing;
    vec2 width = max(fwidth(cell), vec2(1e-6));
    vec2 g = abs(fract(cell - 0.5) - 0.5) / width;
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
    vec3 V = normalize(uCameraPos - vWorldPos);

    float normalLength = length(vNormal);
    vec3 N = normalLength > 1e-6 ? vNormal / normalLength : V;
    if (!gl_FrontFacing) {
        N = -N;
    }
    gBase = uBaseColor;
    gRough = uRoughness;
    gGrazing = 1.0;
    if (uUseMaps != 0 && gl_FrontFacing) {
        applyMaps(N);
    }
    if (uGridOverlay > 0.0) {
        float dist = length(vWorldPos - uCameraPos);
        float lines = max(gridLine(vWorldPos.xz, 1.0) * 0.5, gridLine(vWorldPos.xz, 0.1) * 0.2 * (1.0 - smoothstep(3.0, 10.0, dist)));
        gBase = mix(gBase, vec3(0.2, 0.21, 0.23), lines * uGridOverlay);
    }
    vec3 F0 = mix(vec3(0.04), gBase, uMetallic);

    vec3 color = shade(N, V, normalize(uLightDir), uSunColor, F0);

    for (int i = 0; i < min(uPointCount, kMaxLights); ++i) {
        vec3 toLight = uPointPos[i] - vWorldPos;
        float d2 = max(dot(toLight, toLight), 1e-6);
        float attenuation = min(1.0 / (0.6 + 0.32 * d2), 1.6);
        color += shade(N, V, toLight * inversesqrt(d2), uPointColor[i] * attenuation, F0);
    }

    float NdotV = max(dot(N, V), 1e-4);
    vec3 R = reflect(-V, N);
    vec3 Fa = F0 + (max(vec3(1.0 - gRough), F0) - F0) * pow(1.0 - NdotV, 5.0) * gGrazing;
    color += (1.0 - Fa) * (1.0 - uMetallic) * gBase * environment(N)
           + Fa * mix(environment(R), environment(N), gRough);

    color += uEmissive;
    if (uGhost > 0.001) {
        const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
        ivec2 cell = ivec2(gl_FragCoord.xy / 3.0) + ivec2(0, int(floor(uGhostTime * 6.0)));
        float threshold = (kBayer[(cell.y & 3) * 4 + (cell.x & 3)] + 0.5) / 16.0;
        float rim = pow(1.0 - NdotV, 2.0);

        if (threshold < uGhost * (1.0 - 0.75 * rim)) discard;
        float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(color, vec3(luma) * vec3(0.7, 0.9, 1.0), 0.7 * uGhost);
        color += vec3(0.35, 0.6, 0.75) * rim * uGhost * 0.9;
    }
    color = mix(color, vec3(1.0, 0.45, 0.1), uHighlight * 0.35);

    if (any(isnan(color)) || any(isinf(color))) {
        color = vec3(0.0);
    }
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
