#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "shadow.glsl"
#include "snow_ground.glsl"
#include "lighting.glsl"
#include "clouds.glsl"

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
in vec3 vUp;
in vec3 vObjPos;
in vec3 vObjNormal;
in mat3 vBasis;

out vec4 fragColor;

uniform int uMaps;
uniform vec3 uBaseColor = vec3(1.0);
uniform float uMetallic = 0.0;
uniform float uRoughness = 0.6;
uniform vec3 uEmissive = vec3(0.0);

layout(binding = 0) uniform sampler2D uColorMap;
layout(binding = 4) uniform sampler2D uNormalMap;
layout(binding = 5) uniform sampler2D uSurfaceMap;
layout(binding = 6) uniform sampler2D uBlendMap;
layout(binding = 8) uniform sampler2D uTriGrass;
layout(binding = 9) uniform sampler2D uTriSoil;
layout(binding = 10) uniform sampler2D uTriSoilN;
layout(binding = 11) uniform sampler2D uTriRock;
layout(binding = 12) uniform sampler2D uTriRockN;
layout(binding = 13) uniform sampler2D uTriRockS;

const int MAP_NORMAL = 1;
const int MAP_SURFACE = 2;
const int MAP_BLEND = 4;
const int MAP_GROUND = 8;
const int MAP_PROCEDURAL = 16;

const float GROUND_FAR_START = 18.0;
const float GROUND_FAR_END = 45.0;
const float GROUND_FAR_LOD = 9.0;
const float GROUND_ROUGHNESS = 0.88;
const float WET_DARKEN = 0.28;
const float WET_ROUGHNESS = 0.22;
const float SNOW_ROUGHNESS = 0.7;
const float TRI_GRASS_SCALE = 0.25;
const float TRI_SOIL_SCALE = 0.45;
const float TRI_ROCK_SCALE = 0.3;
const float TRI_ROCK_SCALE_FAR = 0.07;
const float TRI_SHARPNESS = 4.0;

float gCavity = 1.0;

vec3 triWeights(vec3 n) {
    vec3 w = pow(abs(n), vec3(TRI_SHARPNESS));
    return w / max(w.x + w.y + w.z, 1e-5);
}

vec3 triSample(sampler2D t, vec3 p, vec3 w, float scale) {
    return texture(t, p.zy * scale).rgb * w.x + texture(t, p.xz * scale).rgb * w.y + texture(t, p.xy * scale).rgb * w.z;
}

vec3 triNormal(sampler2D t, vec3 p, vec3 n, vec3 w, float scale) {
    vec3 tX = texture(t, p.zy * scale).xyz * 2.0 - 1.0;
    vec3 tY = texture(t, p.xz * scale).xyz * 2.0 - 1.0;
    vec3 tZ = texture(t, p.xy * scale).xyz * 2.0 - 1.0;
    tX = vec3(tX.xy + n.zy, abs(tX.z) * n.x);
    tY = vec3(tY.xy + n.xz, abs(tY.z) * n.y);
    tZ = vec3(tZ.xy + n.xy, abs(tZ.z) * n.z);
    return normalize(tX.zyx * w.x + tY.xzy * w.y + tZ.xyz * w.z);
}

mat3 cotangentFrame(vec3 n, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-20));
    return mat3(t * scale, b * scale, n);
}

void applyProceduralGround(inout vec3 N) {
    vec3 on = normalize(vObjNormal);
    vec3 w = triWeights(on);
    float dist = length(u_cam_pos.xyz - vWorldPos);
    float breakup = fbm2(vObjPos.xz * 0.35 + vObjPos.y * 0.27, 3) - 0.5;
    float c = clamp(vUv.x + breakup * 0.3, 0.0, 1.0);
    float wGrass = 1.0 - smoothstep(0.15, 0.35, c);
    float wRock = smoothstep(0.62, 0.85, c);
    float wSoil = max(1.0 - wGrass - wRock, 0.0);
    vec3 grass = texture(uTriGrass, vObjPos.xz * TRI_GRASS_SCALE).rgb;
    grass = mix(grass, textureLod(uTriGrass, vObjPos.xz * TRI_GRASS_SCALE, GROUND_FAR_LOD).rgb,
                smoothstep(GROUND_FAR_START, GROUND_FAR_END, dist) * 0.8);
    vec3 soil = triSample(uTriSoil, vObjPos, w, TRI_SOIL_SCALE);
    vec3 rock = triSample(uTriRock, vObjPos, w, TRI_ROCK_SCALE);
    vec3 rockFar = triSample(uTriRock, vObjPos, w, TRI_ROCK_SCALE_FAR);
    rock *= mix(vec3(1.0), rockFar / max(luminance(rockFar), 1e-3) * 0.5 + 0.5, 0.6);
    vec3 rockS = triSample(uTriRockS, vObjPos, w, TRI_ROCK_SCALE);
    gBase = grass * wGrass + soil * wSoil + rock * wRock;
    vec3 objN = on;
    if (wRock > 0.0) {
        objN = normalize(mix(objN, triNormal(uTriRockN, vObjPos, on, w, TRI_ROCK_SCALE), wRock));
    }
    if (wSoil > 0.0) {
        objN = normalize(mix(objN, triNormal(uTriSoilN, vObjPos, on, w, TRI_SOIL_SCALE), wSoil));
    }
    N = normalize(vBasis * objN);
    gRough = GROUND_ROUGHNESS * (wGrass + wSoil) + rockS.r * wRock;
    gCavity = vUv.y * mix(1.0, rockS.b, wRock);
}

void main() {
    vec3 V = normalize(u_cam_pos.xyz - vWorldPos);
    float normalLength = length(vNormal);
    vec3 N = normalLength > 1e-6 ? vNormal / normalLength : V;
    vec3 geometric = N;
    vec3 L = sun_toward();

    gBase = texture(uColorMap, vUv).rgb * uBaseColor;
    gRough = uRoughness;
    gMetal = uMetallic;
    gGrazing = 1.0;

    if ((uMaps & MAP_PROCEDURAL) != 0) {
        applyProceduralGround(N);
    }
    if ((uMaps & MAP_NORMAL) != 0) {
        vec3 tangentNormal = texture(uNormalMap, vUv).xyz * 2.0 - 1.0;
        N = normalize(cotangentFrame(geometric, vWorldPos, vUv) * tangentNormal);
    }
    if ((uMaps & MAP_SURFACE) != 0) {
        vec3 surface = texture(uSurfaceMap, vUv).rgb;
        gRough = surface.r;
        gMetal = surface.g;
        gCavity = surface.b;
    }
    if ((uMaps & MAP_BLEND) != 0) {
        float dist = length(u_cam_pos.xyz - vWorldPos);
        vec3 far = textureLod(uColorMap, vUv, GROUND_FAR_LOD).rgb;
        gBase = mix(gBase, far, smoothstep(GROUND_FAR_START, GROUND_FAR_END, dist) * 0.8);
        float jitter = (fbm2(vWorldPos.xz * 0.35 + vWorldPos.y * 0.21, 3) - 0.5) * 0.18;
        float slope = dot(geometric, normalize(vUp));
        float rockiness = 1.0 - smoothstep(0.60 + jitter, 0.78 + jitter, slope);
        gBase = mix(gBase, texture(uBlendMap, vUv * 0.6).rgb, rockiness);
        gRough = GROUND_ROUGHNESS;
    }
    if ((uMaps & MAP_GROUND) != 0) {
        float wetness = u_shadow_params.z;
        float snow = snow_coverage_up(vWorldPos, geometric, normalize(vUp), 0.0);
        if (snow > 0.0) {
            gBase = mix(gBase, snow_albedo(vWorldPos), snow);
            gRough = mix(gRough, SNOW_ROUGHNESS, snow);
            gCavity = mix(gCavity, 1.0, snow);
            wetness *= 1.0 - snow;
        }
        gBase *= 1.0 - wetness * WET_DARKEN;
        gRough = mix(gRough, WET_ROUGHNESS, wetness);
    }
    gRough = clamp(gRough, 0.04, 1.0);

    vec3 F0 = mix(vec3(0.04), gBase, gMetal);
    float shadow = shadow_factor(vWorldPos, max(dot(geometric, L), 0.0)) * cloudShadow(vWorldPos);
    vec3 color = shade(N, V, L, u_sun_color_ambient.rgb, F0) * shadow * gCavity;
    color += shadePoints(vWorldPos, N, V, F0);
    color += ambientLight(N, V, F0) * gCavity;
    color += uEmissive;

    if (any(isnan(color)) || any(isinf(color))) {
        color = vec3(0.0);
    }
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
