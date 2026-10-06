#version 460 core

out vec4 fragColor;

uniform mat4 uInvViewProj;
uniform sampler2D uSceneDepth;
uniform float uSplit;
uniform vec3 uCenter;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uNormal;
uniform float uSize;
uniform float uDepth;
uniform vec3 uColor;
uniform float uOpacity;
uniform float uSeed;
uniform int uKind;

vec3 surfaceAt(ivec2 p, vec2 size, out bool valid) {
    float d = texelFetch(uSceneDepth, p, 0).r;
    valid = d >= uSplit && d < 1.0;
    float z = (d - uSplit) / max(1.0 - uSplit, 1e-6) * 2.0 - 1.0;
    vec2 uv = (vec2(p) + 0.5) / size;
    vec4 w = uInvViewProj * vec4(uv * 2.0 - 1.0, z, 1.0);
    return w.xyz / (abs(w.w) > 1e-8 ? w.w : 1e-8);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 size = vec2(textureSize(uSceneDepth, 0));
    bool valid;
    vec3 world = surfaceAt(p, size, valid);
    if (!valid) discard;

    vec3 rel = world - uCenter;
    vec2 local = vec2(dot(rel, uRight), dot(rel, uUp)) / max(uSize, 1e-4);
    float height = dot(rel, uNormal) / max(uDepth, 1e-4);
    if (abs(local.x) > 1.0 || abs(local.y) > 1.0 || abs(height) > 1.0) discard;

    bool rightValid;
    bool upValid;
    vec3 east = surfaceAt(min(p + ivec2(1, 0), ivec2(size) - 1), size, rightValid) - world;
    vec3 north = surfaceAt(min(p + ivec2(0, 1), ivec2(size) - 1), size, upValid) - world;
    vec3 across = cross(east, north);
    float facing = length(across) > 1e-8 ? abs(dot(normalize(across), uNormal)) : 1.0;
    float side = smoothstep(0.15, 0.45, facing);

    float r = length(local);
    float a = atan(local.y, local.x);
    float density;
    if (uKind == 0) {
        float lumps = 0.12 * sin(a * 3.0 + uSeed) * sin(a * 5.0 + uSeed * 2.3) + 0.06 * sin(a * 9.0 - uSeed);
        density = 1.0 - smoothstep(0.25, 0.95, r + lumps);
        density *= density;
    } else {
        float rough = 0.08 * sin(a * 5.0 + uSeed) + 0.05 * sin(a * 11.0 - uSeed);
        density = 1.0 - smoothstep(0.45, 0.7, r + rough);
    }
    float alpha = density * uOpacity * side * (1.0 - height * height);
    if (!(alpha > 0.002)) discard;
    vec3 color = uColor * alpha;
    if (any(isnan(color)) || any(isinf(color))) discard;
    fragColor = vec4(color, alpha);
}
