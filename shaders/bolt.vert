#version 460 core
#include "frame.glsl"

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_dir;
layout(location = 2) in vec4 a_params;

out float v_across;
out float v_intensity;

const float CORE_PIXELS = 0.85;
const float GLOW_PIXELS = 5.00;

// A discharge channel is centimetres wide, so at true world width it would fall under a
// pixel at any useful distance and drop out entirely rather than dimming. The ribbon is
// expanded here so its width can be floored in pixels instead.
void main()
{
    vec3 to_camera = u_cam_pos.xyz - a_pos;
    float dist = length(to_camera);
    vec3 view = to_camera / max(dist, 1e-4);

    vec3 right = cross(a_dir, view);
    float right_len = length(right);
    right = right_len > 1e-4 ? right / right_len
                             : normalize(cross(a_dir, vec3(0.0, 1.0, 0.0)));

    float metres_per_pixel = dist * 2.0 / (u_proj[1][1] * u_viewport.y);
    float half_width = max(a_params.y, metres_per_pixel * (CORE_PIXELS + GLOW_PIXELS));

    v_across = a_params.x;
    v_intensity = a_params.z;
    gl_Position = u_view_proj * vec4(a_pos + right * (a_params.x * half_width), 1.0);
}
