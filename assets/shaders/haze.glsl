const float HAZE_SKY_DISTANCE = 600.0;
const float HAZE_FADE_SPAN = 0.75;
const float HAZE_INSIDE = 0.4;

float hazeOpticalDepth(vec3 ro, vec3 rd, float dist, float tStart, float tEnd) {
    float depth = 0.0;
    for (int i = 0; i < u_haze_count; i++) {
        vec3 c = u_haze[i].xyz;
        float r = max(u_haze[i].w, 1e-3);
        vec3 oc = ro - c;
        float core = max(r - u_haze_tint.w, 0.0);
        float outside = mix(HAZE_INSIDE, 1.0, smoothstep(core, core + u_haze_tint.w * HAZE_FADE_SPAN, length(oc)));
        float b = dot(oc, rd);
        float h = b * b - (dot(oc, oc) - r * r);
        if (h <= 0.0) {
            continue;
        }
        h = sqrt(h);
        float t0 = max(-b - h, 0.0);
        float t1 = min(-b + h, dist);
        if (t1 - t0 <= 1e-4) {
            continue;
        }
        float closest = length(oc + rd * clamp(-b, t0, t1)) / r;
        float total = (t1 - t0) * (1.0 - closest * closest * 0.7) * outside;
        float share = max(min(t1, tEnd) - max(t0, tStart), 0.0) / (t1 - t0);
        depth += total * share;
    }
    return depth * u_haze_density;
}

vec3 hazeLight() {
    float lum = dot(u_fog_color_density.rgb, vec3(0.3, 0.55, 0.15));
    return u_haze_tint.rgb * (lum * 1.15 + dot(u_sun_color_ambient.rgb, vec3(0.3, 0.55, 0.15)) * 0.02);
}
