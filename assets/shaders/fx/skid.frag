#version 460 core

struct Skid {
    vec4 centerStrength;
    vec4 alongHalfLength;
    vec4 normalHalfWidth;
    vec4 color;
};

layout(std430, binding = 3) readonly buffer Skids {
    Skid skids[];
};

flat in int vIndex;
out vec4 fragColor;

uniform mat4 uInvViewProj;
uniform sampler2D uSceneDepth;
uniform float uSplit;
uniform float uDepth;

vec3 surfaceAt(ivec2 p, vec2 size, out bool valid) {
    float d = texelFetch(uSceneDepth, p, 0).r;
    valid = d >= uSplit && d < 1.0;
    float z = (d - uSplit) / max(1.0 - uSplit, 1e-6) * 2.0 - 1.0;
    vec2 uv = (vec2(p) + 0.5) / size;
    vec4 w = uInvViewProj * vec4(uv * 2.0 - 1.0, z, 1.0);
    return w.xyz / (abs(w.w) > 1e-8 ? w.w : 1e-8);
}

void main() {
    Skid s = skids[vIndex];
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 size = vec2(textureSize(uSceneDepth, 0));
    bool valid;
    vec3 world = surfaceAt(p, size, valid);
    if (!valid) discard;

    vec3 n = s.normalHalfWidth.xyz;
    vec3 along = s.alongHalfLength.xyz;
    vec3 across = cross(n, along);
    across = dot(across, across) > 1e-8 ? normalize(across) : vec3(1.0, 0.0, 0.0);
    vec3 rel = world - s.centerStrength.xyz;
    float halfWidth = max(s.normalHalfWidth.w, 1e-3);
    float ax = dot(rel, along);
    float ay = dot(rel, across) / halfWidth;
    float height = dot(rel, n) / max(uDepth, 1e-4);
    if (ax < -s.alongHalfLength.w || ax >= s.alongHalfLength.w || abs(ay) > 1.0 || abs(height) > 1.0) discard;

    bool rightValid;
    bool upValid;
    vec3 east = surfaceAt(min(p + ivec2(1, 0), ivec2(size) - 1), size, rightValid) - world;
    vec3 north = surfaceAt(min(p + ivec2(0, 1), ivec2(size) - 1), size, upValid) - world;
    vec3 facing = cross(east, north);
    float side = length(facing) > 1e-8 ? smoothstep(0.3, 0.6, abs(dot(normalize(facing), n))) : 1.0;

    float edge = 1.0 - smoothstep(0.55, 1.0, abs(ay));
    float tread = 0.82 + 0.18 * sin(ay * 9.0);
    float alpha = s.color.a * s.centerStrength.w * edge * tread * side * (1.0 - height * height);
    if (!(alpha > 0.002)) discard;
    vec3 color = s.color.rgb * alpha;
    if (any(isnan(color)) || any(isinf(color))) discard;
    fragColor = vec4(color, alpha);
}
