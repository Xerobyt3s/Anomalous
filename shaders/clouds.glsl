// High cirrus.
//
// The whole point of this layer is that it is lit by sunlight sampled at 6 km, not at the
// ground. At this sun angle the light reaching cloud altitude has travelled through far
// less dense air than the light reaching us, so it is much brighter and only moderately
// reddened - which is exactly why sunset cirrus burns pink and gold against a sky that
// has already gone cool.
#ifndef ANOM_CLOUDS_GLSL
#define ANOM_CLOUDS_GLSL

#include "common.glsl"
#include "frame.glsl"
#include "atmosphere.glsl"

const float kCirrusAltitude = 6.0;   // km
const float kCirrusCoverage = 0.46;  // higher leaves less sky covered
const float kCirrusOpacity = 0.78;

// Returns rgb radiance and coverage alpha.
vec4 cirrusLayer(float originAltitude, vec3 direction, vec3 sunDirection) {
    if (direction.y <= 0.004) return vec4(0.0);

    float distanceToLayer = raySphere(originAltitude, direction.y, kCirrusAltitude).y;
    if (distanceToLayer <= 0.0) return vec4(0.0);

    vec3 origin = vec3(0.0, kPlanetRadius + originAltitude, 0.0);
    vec2 p = (origin + direction * distanceToLayer).xz;

    // Drift, in km/s. Deliberately slow - this should be noticeable only if you watch.
    p += vec2(u_time_seconds * 0.0045, u_time_seconds * 0.0016);

    // Anisotropic sampling: compressing one axis makes the noise form long streaks
    // instead of blobs, which is the difference between cirrus and cotton wool.
    // Frequency is set by what the layer looks like overhead, where it is only ~6 km away
    // and a small angular area maps to a small physical one. Too low and the zenith is a
    // single smooth blob while the horizon still looks fine.
    vec2 q = vec2(p.x * 0.170, p.y * 0.048);

    float shape = fbm2(q, 4) + 0.5;
    shape += fbm2(q * 3.1, 3) * 0.25;

    float density = smoothstep(kCirrusCoverage, kCirrusCoverage + 0.30, shape);
    // Erode the body into wisps rather than leaving solid slabs.
    density *= 0.45 + 0.55 * smoothstep(0.15, 0.85, fbm2(q * 7.0, 2) + 0.5);

    // Near the horizon the layer is seen almost edge-on through a great deal of air;
    // fade it into the haze rather than letting it run into a hard band.
    density *= smoothstep(0.004, 0.10, direction.y);

    if (density <= 0.002) return vec4(0.0);

    // Thin ice cloud scatters strongly forward, so the cirrus nearest the sun lights up
    // far more than the rest of the sky.
    // Sunlight sampled at *this* cloud's position, not one reference point overhead.
    // Cirrus near the horizon is a hundred kilometres away, sitting in light that has
    // taken a very different path to get there, and it is that variation across the layer
    // - warm gold near the sun, cooling and reddening away from it - that makes the sky
    // read as depth rather than as one flat sheet. sunTransmittance also handles the
    // planet's own shadow, so cloud past the terminator simply goes unlit.
    vec3 cloudPoint = origin + direction * distanceToLayer;
    vec3 sunAtCloud = kSunIrradiance * sunTransmittance(cloudPoint, sunDirection);

    // Scaled to keep cloud away from the sun below 1.0. Above that AgX compresses and
    // desaturates towards white, throwing away the very colour this layer exists for.
    float forward = henyeyGreenstein(dot(direction, sunDirection), 0.62);
    vec3 color = sunAtCloud * (0.020 + 0.50 * forward);
    // Skylight falling on the layer from above.
    color += u_cloud_sun_color.rgb * 0.0016 + u_sky_ambient.rgb * 0.22;

    // We are looking at these from underneath. Thicker parts let less light through, so
    // the body darkens while the wispy edges stay bright - that contrast is what makes
    // the lit edges read as pink rather than the whole layer as flat grey.
    color *= mix(1.0, 0.5, density);

    return vec4(color, density * kCirrusOpacity);
}

#endif
