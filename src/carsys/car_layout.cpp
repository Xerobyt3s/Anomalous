#include "carsys/car_layout.h"

#include "carsys/parts.h"
#include "core/config.h"

#include <cstdio>

namespace anom {
namespace {

CarLayout g_layout;

void read_mesh(const Config& cfg, const char* key, FixedString<32>& out)
{
    if (cfg.has(key)) {
        out.assign(cfg.get_str(key, ""));
    }
}

void read_vec(const Config& cfg, const char* key, Vec3& out)
{
    out = cfg.get_vec3(key, out);
}

void read_f32(const Config& cfg, const char* key, f32& out)
{
    out = cfg.get_f32(key, out);
}

void read_list(const Config& cfg, const char* key, f32* out, u32 count)
{
    f32 values[8] = {};
    if (cfg.get_f32_list(key, std::span<f32>(values, count)) == count) {
        for (u32 i = 0; i < count; i++) {
            out[i] = values[i];
        }
    }
}

bool read_box(const Config& cfg, const char* key, LayoutBox& out)
{
    f32 values[6] = {};
    if (cfg.get_f32_list(key, std::span<f32>(values, 6)) != 6) {
        return false;
    }
    out.center = Vec3{values[0], values[1], values[2]};
    out.half = Vec3{values[3], values[4], values[5]};
    return true;
}

} // namespace

void car_layout_parse(CarLayout& out, const Config& cfg)
{
    out = CarLayout{};
    read_mesh(cfg, "layout.hood_mesh", out.hood_mesh);
    read_mesh(cfg, "layout.trunk_mesh", out.trunk_mesh);
    read_mesh(cfg, "layout.door_l_mesh", out.door_mesh[0]);
    read_mesh(cfg, "layout.door_r_mesh", out.door_mesh[1]);
    read_mesh(cfg, "layout.door_glass_l_mesh", out.door_glass_mesh[0]);
    read_mesh(cfg, "layout.door_glass_r_mesh", out.door_glass_mesh[1]);
    read_mesh(cfg, "layout.glass_mesh", out.glass_mesh);
    read_mesh(cfg, "layout.brakelight_mesh", out.brakelight_mesh);
    read_mesh(cfg, "layout.revlight_mesh", out.revlight_mesh);
    read_mesh(cfg, "layout.popup_mesh", out.popup_mesh);
    read_mesh(cfg, "layout.fuelcap_mesh", out.fuelcap_mesh);
    read_mesh(cfg, "layout.lever_mesh", out.lever_mesh);
    read_mesh(cfg, "layout.interior_mesh", out.interior_mesh);

    read_vec(cfg, "layout.hood_hinge", out.hood_hinge);
    read_f32(cfg, "layout.hood_angle", out.hood_angle);
    read_vec(cfg, "layout.popup_offset", out.popup_offset);
    read_vec(cfg, "layout.door_hinge", out.door_hinge);
    read_f32(cfg, "layout.door_angle", out.door_angle);
    read_vec(cfg, "layout.trunk_hinge", out.trunk_hinge);
    read_f32(cfg, "layout.trunk_angle", out.trunk_angle);

    read_vec(cfg, "layout.lever", out.lever);
    read_vec(cfg, "layout.fuelcap", out.fuelcap);
    read_vec(cfg, "layout.key", out.key);
    read_vec(cfg, "layout.deck", out.deck);
    read_list(cfg, "layout.dial_x", out.dial_x, 4);
    read_list(cfg, "layout.dial_y", out.dial_y, 4);
    read_f32(cfg, "layout.dial_z", out.dial_z);
    read_list(cfg, "layout.dial_scale", out.dial_scale, 4);
    read_vec(cfg, "layout.warn_first", out.warn_first);
    read_f32(cfg, "layout.warn_step", out.warn_step);

    read_list(cfg, "layout.wiper_x", out.wiper_x, 2);
    read_vec(cfg, "layout.wiper_base", out.wiper_base);
    read_vec(cfg, "layout.wiper_axis", out.wiper_axis);
    read_f32(cfg, "layout.wiper_glass_y", out.wiper_glass_y);
    read_f32(cfg, "layout.wiper_reach", out.wiper_reach);

    read_vec(cfg, "layout.head_lamp", out.head_lamp);
    read_vec(cfg, "layout.head_aim", out.head_aim);
    read_vec(cfg, "layout.head_fill", out.head_fill);
    read_vec(cfg, "layout.tail_lamp", out.tail_lamp);
    read_vec(cfg, "layout.reverse_lamp", out.reverse_lamp);

    read_vec(cfg, "layout.jack_coax", out.jack_coax);
    read_vec(cfg, "layout.jack_bus", out.jack_bus);
    read_vec(cfg, "layout.antenna_jack", out.antenna_jack);
    read_vec(cfg, "layout.bay_jack", out.bay_jack);
    read_vec(cfg, "layout.printer_jack", out.printer_jack);

    read_vec(cfg, "layout.trunk_min", out.trunk_min);
    read_vec(cfg, "layout.trunk_max", out.trunk_max);

    read_vec(cfg, "layout.hood_point", out.hood_point);
    read_vec(cfg, "layout.trunk_point", out.trunk_point);
    read_vec(cfg, "layout.engine_point", out.engine_point);
    read_vec(cfg, "layout.door_sound", out.door_sound);
    read_vec(cfg, "layout.steam_point", out.steam_point);
    read_vec(cfg, "layout.jerrycan_from", out.jerrycan_from);
    read_vec(cfg, "layout.jerrycan_to", out.jerrycan_to);
    read_vec(cfg, "layout.press_center", out.press_center);
    read_vec(cfg, "layout.press_half", out.press_half);
    read_vec(cfg, "layout.engine_audio", out.engine_audio);
    read_vec(cfg, "layout.dash_audio", out.dash_audio);
    read_f32(cfg, "layout.chase_height", out.chase_height);
    read_f32(cfg, "layout.chase_near", out.chase_near);
    read_f32(cfg, "layout.chase_far", out.chase_far);

    char key[48];
    for (u32 i = 0; i < kLayoutParts; i++) {
        std::snprintf(key, sizeof(key), "layout.socket%u", i);
        out.socket_set[i] = read_box(cfg, key, out.sockets[i]);
    }
    u32 boxes = 0;
    for (u32 i = 0; i < kLayoutCableBoxes; i++) {
        std::snprintf(key, sizeof(key), "layout.cable_box%u", i);
        boxes += read_box(cfg, key, out.cable_boxes[i]) ? 1u : 0u;
    }
    out.cable_boxes_set = boxes == kLayoutCableBoxes;
}

const CarLayout& car_layout()
{
    return g_layout;
}

void set_car_layout(const CarLayout& layout)
{
    g_layout = layout;
    apply_part_layout(g_layout);
}

}
