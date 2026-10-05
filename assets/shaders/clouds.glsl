#ifndef ANOM_CLOUDS_GLSL
#define ANOM_CLOUDS_GLSL

#include "common.glsl"
#include "frame.glsl"
#include "atmosphere.glsl"
#include "fx/noise.glsl"

const float kCloudBottom = 1.2;
const float kCloudTop = 3.4;
const float kCloudExtinction = 22.0;
const int kCloudSteps = 40;
const int kCloudLightSteps = 5;
const float kCloudLightStep = 0.18;
const float kCloudMaxMarch = 28.0;
const float kCloudCoverageClear = 0.58;
const float kCloudCoverageOvercast = 1.05;
const float kCloudCoverageScale = 0.11;
const float kCloudShapeScale = 1.25;
const float kCloudDetailScale = 5.5;
const float kCloudSunScale = 0.85;
const float kCloudAmbientScale = 1.15;
const float kCloudFadeKm = 55.0;
const float kCloudShadowStrength = 0.62;
const vec2 kCloudWindDir = vec2(0.86, 0.51);

float cloudCoverageAmount() {
    return mix(kCloudCoverageClear, kCloudCoverageOvercast, clamp(u_shadow_params.w, 0.0, 1.0));
}

vec2 cloudDrift() {
    return kCloudWindDir * (0.006 + u_wind * 0.004) * u_time_seconds;
}

float cloudCoverage(vec2 xzKm) {
    float amount = cloudCoverageAmount();
    float c = n_fbm((xzKm + cloudDrift()) * kCloudCoverageScale);
    return smoothstep(1.0 - amount, 1.0 - amount + 0.35, c + 0.1);
}

float cloudDensity(vec3 pKm, float height01) {
    float coverage = cloudCoverage(pKm.xz);
    if (coverage <= 0.001) {
        return 0.0;
    }
    float profile = smoothstep(0.0, 0.12, height01) * (1.0 - smoothstep(0.45, 1.0, height01));
    vec3 drift = vec3(cloudDrift().x, 0.0, cloudDrift().y);
    float shape = n_fbm3(vec3(pKm.x, pKm.y * 1.7, pKm.z) * kCloudShapeScale + drift);
    float threshold = 1.0 - coverage * profile;
    float d = clamp((shape - threshold) / max(1.0 - threshold, 0.05), 0.0, 1.0);
    if (d <= 0.0) {
        return 0.0;
    }
    float detail = n_fbm3(pKm * kCloudDetailScale + drift * 3.0);
    return clamp(d - (1.0 - detail) * 0.3 * (1.0 - d), 0.0, 1.0);
}

float cloudShadow(vec3 worldMeters) {
    vec3 sun = sun_toward();
    if (sun.y <= 0.02) {
        return 1.0;
    }
    float midKm = (kCloudBottom + kCloudTop) * 0.5;
    float t = (midKm * 1000.0 - worldMeters.y) / sun.y;
    vec2 xzKm = (worldMeters.xz + sun.xz * t) * 0.001;
    return 1.0 - kCloudShadowStrength * smoothstep(0.15, 0.7, cloudCoverage(xzKm));
}

float cloudPhase(float cosTheta) {
    return mix(henyeyGreenstein(cosTheta, 0.6), henyeyGreenstein(cosTheta, -0.25), 0.3);
}

vec4 marchClouds(vec3 direction, float jitter) {
    if (direction.y <= 0.01) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    float altitude = worldAltitude(u_cam_pos.xyz);
    float t0 = raySphere(altitude, direction.y, kCloudBottom).y;
    float t1 = raySphere(altitude, direction.y, kCloudTop).y;
    if (t0 <= 0.0 || t1 <= t0) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    t1 = min(t1, t0 + kCloudMaxMarch);
    float ds = (t1 - t0) / float(kCloudSteps);
    vec3 origin = vec3(u_cam_pos.x * 0.001, kPlanetRadius + altitude, u_cam_pos.z * 0.001);
    vec3 sun = sun_toward();
    float phase = cloudPhase(dot(direction, sun));
    float overcast = clamp(u_shadow_params.w, 0.0, 1.0);
    float sunDim = 1.0 - 0.6 * overcast;
    float ambientDim = 1.0 - 0.45 * overcast;

    vec3 color = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < kCloudSteps; ++i) {
        float t = t0 + (float(i) + jitter) * ds;
        vec3 p = origin + direction * t;
        float pAltitude = length(p) - kPlanetRadius;
        float height01 = clamp((pAltitude - kCloudBottom) / (kCloudTop - kCloudBottom), 0.0, 1.0);
        float d = cloudDensity(vec3(p.x, pAltitude, p.z), height01);
        if (d <= 0.0) {
            continue;
        }
        float lightDepth = 0.0;
        for (int k = 1; k <= kCloudLightSteps; ++k) {
            vec3 q = p + sun * (float(k) * kCloudLightStep);
            float qAltitude = length(q) - kPlanetRadius;
            float qHeight = (qAltitude - kCloudBottom) / (kCloudTop - kCloudBottom);
            if (qHeight > 1.0) {
                break;
            }
            lightDepth += cloudDensity(vec3(q.x, qAltitude, q.z), clamp(qHeight, 0.0, 1.0));
        }
        float sunVisible = exp(-kCloudExtinction * lightDepth * kCloudLightStep);
        vec3 sunAtCloud = kSunIrradiance * sunTransmittance(p, sun) * kCloudSunScale * sunDim;
        vec3 ambient = u_sky_ambient.rgb * mix(0.55, 1.0, height01) * kCloudAmbientScale * ambientDim;
        vec3 scatter = sunAtCloud * sunVisible * phase + ambient;
        float stepTransmittance = exp(-kCloudExtinction * d * ds);
        color += transmittance * scatter * (1.0 - stepTransmittance);
        transmittance *= stepTransmittance;
        if (transmittance < 0.02) {
            break;
        }
    }
    float fade = smoothstep(0.01, 0.09, direction.y) * exp(-t0 / kCloudFadeKm);
    return vec4(color * fade, mix(1.0, transmittance, fade));
}

#endif
