#ifndef GHOST_LIGHTING_GLSL
#define GHOST_LIGHTING_GLSL

#include "frame.glsl"

vec3 gBase;
float gRough;
float gMetal;
float gGrazing;

const float LIGHT_PI = 3.14159265;

vec3 environment(vec3 dir) {
    return ambient_for_normal(dir);
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
    float D = alpha2 / (LIGHT_PI * denom * denom);
    float k = (gRough + 1.0) * (gRough + 1.0) / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    vec3 specular = D * G * F / (4.0 * NdotV * NdotL + 1e-4);
    vec3 diffuse = (1.0 - F) * (1.0 - gMetal) * gBase / LIGHT_PI;
    return (diffuse + specular) * radiance * NdotL;
}

float pointAttenuation(float distanceSquared) {
    return min(1.0 / (0.6 + 0.32 * distanceSquared), 1.6);
}

vec3 shadePoints(vec3 world, vec3 N, vec3 V, vec3 F0) {
    vec3 color = vec3(0.0);
    for (int i = 0; i < min(u_point_count, 8); ++i) {
        vec3 toLight = u_point_pos[i].xyz - world;
        float d2 = max(dot(toLight, toLight), 1e-6);
        color += shade(N, V, toLight * inversesqrt(d2), u_point_color[i].rgb * pointAttenuation(d2), F0);
    }
    return color;
}

vec3 pointRadiance(vec3 world) {
    vec3 light = vec3(0.0);
    for (int i = 0; i < min(u_point_count, 8); ++i) {
        vec3 toLight = u_point_pos[i].xyz - world;
        light += u_point_color[i].rgb * pointAttenuation(max(dot(toLight, toLight), 1e-6));
    }
    return light;
}

vec3 ambientLight(vec3 N, vec3 V, vec3 F0) {
    float NdotV = max(dot(N, V), 1e-4);
    vec3 R = reflect(-V, N);
    vec3 Fa = F0 + (max(vec3(1.0 - gRough), F0) - F0) * pow(1.0 - NdotV, 5.0) * gGrazing;
    return (1.0 - Fa) * (1.0 - gMetal) * gBase * environment(N) + Fa * mix(environment(R), environment(N), gRough);
}

#endif
