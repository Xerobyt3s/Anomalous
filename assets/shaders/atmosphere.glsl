// Single-scattering atmosphere: Rayleigh + Mie + ozone, raymarched.
//
// This is what makes the sunset glow instead of looking like an orange gradient,
// and it is the source of truth for every light in the scene - src/sky.cpp mirrors
// these constants and integrals on the CPU to derive sun and ambient colours.
// KEEP THE TWO IN SYNC.
//
// Units are kilometres; coordinates are relative to the planet centre.
#ifndef ANOM_ATMOSPHERE_GLSL
#define ANOM_ATMOSPHERE_GLSL

#include "common.glsl"

const float kPlanetRadius = 6360.0;
const float kAtmosphereRadius = 6460.0;

const vec3 kBetaRayleigh = vec3(5.802e-3, 13.558e-3, 33.1e-3);
// Hazier than a clean reference atmosphere. Aerosol forward-scattering is what
// produces the deep orange-red band at a low sun; at the textbook value the horizon
// comes out a washed pale yellow.
const float kBetaMieScatter = 10.0e-3;
const float kBetaMieAbsorb = 4.4e-3;
const vec3 kBetaOzone = vec3(0.650e-3, 1.881e-3, 0.085e-3);

const float kHeightRayleigh = 8.0;
const float kHeightMie = 1.2;
const float kOzoneCentre = 25.0;
const float kOzoneHalfWidth = 15.0;

const float kMieG = 0.76;
const float kSunAngularRadius = 0.0047;  // ~0.27 degrees
const vec3 kSunIrradiance = vec3(20.0);
const vec3 kGroundAlbedo = vec3(0.10, 0.09, 0.07);

const int kViewSteps = 32;
const int kLightSteps = 8;

// Higher-order scattering, approximated. Real sunset skies get much of their light
// from photons that bounced more than once; single scattering alone renders a muddy
// grey sky because the sunlight reaching nearby air is already heavily reddened.
// The fill light is therefore sampled high in the atmosphere, where the path to the
// sun is short and still blue, and treated as isotropic.
const float kMultipleScatterStrength = 0.15;
const float kMultipleScatterAltitude = 30.0;  // km

// Nearest/farthest intersection with a sphere centred on the planet, or -1.
//
// Parameterised by altitude rather than absolute position. The naive form computes
// dot(O,O) - R^2, which at planetary scale subtracts two values around 4e7 to get a
// result around 1e2 - float keeps about seven digits, so almost all of it is noise.
// That made the grazing horizon test disagree between neighbouring pixels and showed
// up as dark teeth along the skyline. Writing the same quantity as
// (r - Rs)(r + Rs) = (altitude - sphereAltitude)(2R + altitude + sphereAltitude)
// never forms the large intermediate values at all.
vec2 raySphere(float altitude, float cosZenith, float sphereAltitude) {
    float radius = kPlanetRadius + altitude;
    float b = radius * cosZenith;
    float c = (altitude - sphereAltitude) * (2.0 * kPlanetRadius + altitude + sphereAltitude);
    float discriminant = b * b - c;
    if (discriminant < 0.0) return vec2(-1.0);
    discriminant = sqrt(discriminant);
    return vec2(-b - discriminant, -b + discriminant);
}

// For sample points off the vertical axis, where the altitude has to be recovered from
// the position. Less exact than the form above but only used away from the horizon.
vec2 raySphereAt(vec3 position, vec3 direction, float sphereAltitude) {
    float altitude = length(position) - kPlanetRadius;
    float cosZenith = dot(normalize(position), direction);
    return raySphere(altitude, cosZenith, sphereAltitude);
}

const float kAtmosphereThickness = kAtmosphereRadius - kPlanetRadius;

// x: Rayleigh, y: Mie, z: ozone. Ozone uses the usual tent profile.
vec3 atmosphereDensity(float altitude) {
    return vec3(exp(-altitude / kHeightRayleigh), exp(-altitude / kHeightMie),
                max(0.0, 1.0 - abs(altitude - kOzoneCentre) / kOzoneHalfWidth));
}

vec3 extinctionCoefficients(vec3 density) {
    return kBetaRayleigh * density.x + (kBetaMieScatter + kBetaMieAbsorb) * density.y +
           kBetaOzone * density.z;
}

// Transmittance from `position` along `direction` for `rayLength` kilometres.
//
// Steps are distributed quadratically so samples cluster near the ray origin where
// the atmosphere is densest. Uniform steps badly under-sample grazing rays - with a
// low sun the first sample lands kilometres up, the dense air near the ground is
// missed entirely, and the sun comes out white instead of red.
vec3 atmosphereTransmittance(vec3 position, vec3 direction, float rayLength) {
    if (rayLength <= 0.0) return vec3(1.0);
    vec3 opticalDepth = vec3(0.0);
    float previous = 0.0;
    for (int i = 0; i < kLightSteps; ++i) {
        float fraction = float(i + 1) / float(kLightSteps);
        float next = rayLength * fraction * fraction;
        float segment = next - previous;
        vec3 samplePosition = position + direction * (previous + segment * 0.5);
        float altitude = length(samplePosition) - kPlanetRadius;
        opticalDepth += extinctionCoefficients(atmosphereDensity(altitude)) * segment;
        previous = next;
    }
    return exp(-opticalDepth);
}

// Transmittance towards the sun, accounting for the planet's own shadow.
vec3 sunTransmittance(vec3 position, vec3 sunDirection) {
    if (raySphereAt(position, sunDirection, 0.0).x > 0.0) return vec3(0.0);
    float toEdge = raySphereAt(position, sunDirection, kAtmosphereThickness).y;
    return atmosphereTransmittance(position, sunDirection, toEdge);
}

// Wavelength-dependent limb darkening - the sun's edge is redder than its centre.
vec3 sunLimbDarkening(float centreOffset) {
    float mu = sqrt(max(0.0, 1.0 - centreOffset * centreOffset));
    return pow(vec3(mu), vec3(0.397, 0.503, 0.652));
}

// How much of the scattering mass is still in sunlight, which is what ends the day.
//
// Earth's shadow rises as R*alpha^2/2 with the sun's depression alpha - 8.7 km at 3
// degrees, 24 km at 5 - and only the air above it can feed the multiple-scattering
// approximation. Weighting that height against the Rayleigh scale height turns "how
// high is the shadow" into "how much of the scattering air is still lit".
//
// Without this the isotropic fill stays at full daytime strength until its 30 km probe
// crosses the terminator at about -5.5 degrees, and then collapses in a single degree.
// In between it swamps single scattering, and since it carries no directional
// information at all the twilight sky comes out the same colour in every azimuth -
// measured at -3 degrees, the anti-sun horizon was as pink as the sunward one.
//
// Exactly 1.0 for a sun at or above the horizon, so nothing above the horizon moves.
float sunlitAirFraction(vec3 sunDirection) {
    float depression = max(-sunDirection.y, 0.0);  // sin(alpha) ~ alpha at these angles
    float shadowHeight = kPlanetRadius * depression * depression * 0.5;
    return exp(-shadowHeight / kHeightRayleigh);
}

// Isotropic fill representing higher-order scattering. See the note by
// kMultipleScatterStrength for why the light is sampled high in the atmosphere.
vec3 multipleScatterFill(vec3 sunDirection) {
    vec3 highAltitude = vec3(0.0, kPlanetRadius + kMultipleScatterAltitude, 0.0);
    // Folded into one scalar rather than multiplied in afterwards: above the horizon the
    // expression then has to be *textually* the one the sunset was tuned against. An
    // extra `* 1.0` is mathematically free but not bitwise free - the compiler
    // reassociates, and 6-bit quantisation turns a last-bit difference into a visibly
    // different pixel. Exactly one pixel, when this was measured.
    float strength = sunDirection.y >= 0.0
                         ? kMultipleScatterStrength
                         : kMultipleScatterStrength * sunlitAirFraction(sunDirection);
    return kSunIrradiance * sunTransmittance(highAltitude, sunDirection) * strength *
           (1.0 / (4.0 * PI));
}

// Aerial perspective over a finite segment: what the air between the camera and a
// surface scatters in, and how much of that surface's own light survives the trip.
// Using the same integral as the sky means distant terrain converges exactly onto the
// sky colour, so the horizon has no seam.
vec3 aerialPerspective(float originAltitude, vec3 direction, vec3 sunDirection, float distanceKm,
                       out vec3 transmittance) {
    const int kAerialSteps = 8;  // the segment is short, density barely varies

    vec3 origin = vec3(0.0, kPlanetRadius + originAltitude, 0.0);
    vec3 opticalDepth = vec3(0.0);
    vec3 inscatter = vec3(0.0);
    float stepSize = distanceKm / float(kAerialSteps);

    float cosTheta = dot(direction, sunDirection);
    float phaseRayleigh = rayleighPhase(cosTheta);
    float phaseMie = henyeyGreenstein(cosTheta, kMieG);
    vec3 fillLight = multipleScatterFill(sunDirection);

    for (int i = 0; i < kAerialSteps; ++i) {
        vec3 position = origin + direction * ((float(i) + 0.5) * stepSize);
        float altitude = length(position) - kPlanetRadius;
        vec3 density = atmosphereDensity(altitude) * stepSize;

        opticalDepth += extinctionCoefficients(density);
        vec3 viewTransmittance = exp(-opticalDepth);

        vec3 scatterRayleigh = kBetaRayleigh * density.x;
        vec3 scatterMie = vec3(kBetaMieScatter * density.y);

        inscatter += viewTransmittance * sunTransmittance(position, sunDirection) *
                     (scatterRayleigh * phaseRayleigh + scatterMie * phaseMie) * kSunIrradiance;
        inscatter += viewTransmittance * (scatterRayleigh + scatterMie) * fillLight;
    }

    transmittance = exp(-opticalDepth);
    return inscatter;
}

// Full radiance along a view ray, including the sun disk and distant hazy ground.
vec3 atmosphereRadiance(float originAltitude, vec3 direction, vec3 sunDirection,
                        bool includeSunDisk) {
    // The origin is always on the +Y axis, so the zenith cosine is just direction.y.
    vec3 origin = vec3(0.0, kPlanetRadius + originAltitude, 0.0);
    vec2 atmosphereHit = raySphere(originAltitude, direction.y, kAtmosphereThickness);
    if (atmosphereHit.y < 0.0) return vec3(0.0);

    float rayStart = max(atmosphereHit.x, 0.0);
    float rayEnd = atmosphereHit.y;

    vec2 planetHit = raySphere(originAltitude, direction.y, 0.0);
    bool hitGround = planetHit.x > 0.0;
    if (hitGround) rayEnd = planetHit.x;

    float span = rayEnd - rayStart;
    vec3 opticalDepth = vec3(0.0);
    vec3 scatteredRayleigh = vec3(0.0);
    vec3 scatteredMie = vec3(0.0);
    vec3 scatteredMultiple = vec3(0.0);
    float previous = rayStart;

    // Same quadratic distribution as the transmittance integral, for the same reason.
    for (int i = 0; i < kViewSteps; ++i) {
        float fraction = float(i + 1) / float(kViewSteps);
        float next = rayStart + span * fraction * fraction;
        float segment = next - previous;

        vec3 position = origin + direction * (previous + segment * 0.5);
        float altitude = length(position) - kPlanetRadius;
        vec3 density = atmosphereDensity(altitude) * segment;

        opticalDepth += extinctionCoefficients(density);
        vec3 viewTransmittance = exp(-opticalDepth);
        vec3 combined = viewTransmittance * sunTransmittance(position, sunDirection);

        scatteredRayleigh += combined * density.x;
        scatteredMie += combined * density.y;
        scatteredMultiple +=
            viewTransmittance * (kBetaRayleigh * density.x + kBetaMieScatter * density.y);
        previous = next;
    }

    float cosTheta = dot(direction, sunDirection);
    vec3 radiance = (scatteredRayleigh * kBetaRayleigh * rayleighPhase(cosTheta) +
                     scatteredMie * kBetaMieScatter * henyeyGreenstein(cosTheta, kMieG)) *
                    kSunIrradiance;

    radiance += scatteredMultiple * multipleScatterFill(sunDirection);

    // No surface term for the planet. Before the terrain existed, a virtual ground here
    // stood in for distant land - but its horizon lies ~51 km away, while the terrain
    // mesh ends at 3 km, so the two disagreed exactly along the skyline and produced a
    // row of dark teeth. Below the horizon this now returns pure inscattering, which is
    // precisely what the terrain shader fades into at its rim, so the seam vanishes.
    // The ray is still clipped at the planet so nothing integrates through it.

    if (includeSunDisk && !hitGround) {
        float angle = acos(clamp(cosTheta, -1.0, 1.0));
        if (angle < kSunAngularRadius) {
            // Radiance of the disk = irradiance spread over its solid angle.
            float solidAngle = TAU * (1.0 - cos(kSunAngularRadius));
            vec3 disk = kSunIrradiance / solidAngle;
            disk *= sunLimbDarkening(angle / kSunAngularRadius);
            float toEdge = raySphere(originAltitude, sunDirection.y, kAtmosphereThickness).y;
            disk *= atmosphereTransmittance(origin, sunDirection, toEdge);
            radiance += disk;
        }
    }

    return radiance;
}

// World position (metres, y = 0 at the water) -> altitude in kilometres. Horizontal
// displacement is irrelevant at planetary scale. Carrying the altitude rather than an
// absolute position is what keeps raySphere numerically sound.
float worldAltitude(vec3 worldPosition) { return 0.2 + worldPosition.y * 0.001; }

#endif
