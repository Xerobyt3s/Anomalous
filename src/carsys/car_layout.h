#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class Config;

inline constexpr u32 kLayoutParts = 16;
inline constexpr u32 kLayoutCableBoxes = 14;

struct LayoutBox {
    Vec3 center{};
    Vec3 half{};
};

struct CarLayout {
    FixedString<32> hood_mesh{"excel_hood"};
    FixedString<32> trunk_mesh{"excel_trunk_lid"};
    FixedString<32> door_mesh[2] = {FixedString<32>{"excel_door_l"}, FixedString<32>{"excel_door_r"}};
    FixedString<32> door_glass_mesh[2] = {FixedString<32>{"excel_door_glass_l"}, FixedString<32>{"excel_door_glass_r"}};
    FixedString<32> glass_mesh{"excel_glass"};
    FixedString<32> brakelight_mesh{"excel_brakelight"};
    FixedString<32> revlight_mesh{"excel_revlight"};
    FixedString<32> popup_mesh{"excel_popup"};
    FixedString<32> fuelcap_mesh{"excel_fuelcap"};
    FixedString<32> lever_mesh{"excel_lever"};
    FixedString<32> interior_mesh{""};
    FixedString<32> tire_mesh{"excel_wheel"};

    Vec3 hood_hinge{0.0f, 0.1474f, -0.62f};
    f32 hood_angle = 1.15f;
    Vec3 popup_offset{0.40f, -0.0934f, -0.88f};
    Vec3 door_hinge{0.80f, 0.0f, -0.55f};
    f32 door_angle = 1.05f;
    Vec3 trunk_hinge{0.0f, 0.2038f, 1.42f};
    f32 trunk_angle = 1.35f;

    Vec3 lever{-0.13f, -0.17f, 0.44f};
    Vec3 fuelcap{0.80f, 0.145f, 1.30f};
    Vec3 key{-0.22f, 0.05f, -0.25f};
    Vec3 deck{0.12f, -0.045f, -0.295f};
    f32 dial_x[4] = {-0.44f, -0.30f, -0.405f, -0.335f};
    f32 dial_y[4] = {0.064f, 0.064f, 0.006f, 0.006f};
    f32 dial_z = -0.305f;
    f32 dial_scale[4] = {1.0f, 1.0f, 0.5f, 0.5f};
    Vec3 warn_first{-0.418f, 0.030f, -0.304f};
    f32 warn_step = 0.028f;

    f32 wiper_x[2] = {-0.38f, 0.10f};
    Vec3 wiper_base{0.0f, 0.150f, -0.60f};
    Vec3 wiper_axis{0.0f, 0.807f, -0.591f};
    f32 wiper_glass_y = 0.094f;
    f32 wiper_reach = 0.52f;

    Vec3 head_lamp{0.40f, 0.10f, -1.60f};
    Vec3 head_aim{0.0f, -0.07f, -1.0f};
    Vec3 head_fill{0.0f, 0.15f, -2.2f};
    Vec3 tail_lamp{0.0f, 0.05f, 2.3f};
    Vec3 reverse_lamp{0.0f, -0.05f, 2.4f};

    Vec3 jack_coax{0.35f, 0.515f, 0.36f};
    Vec3 jack_bus{0.32f, -0.02f, -0.75f};
    Vec3 antenna_jack{0.35f, 0.54f, 0.36f};
    Vec3 bay_jack{0.32f, 0.02f, -0.75f};
    Vec3 printer_jack{0.42f, -0.17f, 0.695f};

    Vec3 trunk_min{-0.50f, -0.20f, 1.45f};
    Vec3 trunk_max{0.50f, 0.16f, 2.03f};

    Vec3 hood_point{0.0f, 0.45f, -1.4f};
    Vec3 trunk_point{0.0f, 0.45f, 1.6f};
    Vec3 engine_point{0.0f, 0.10f, -1.55f};
    Vec3 door_sound{0.9f, 0.1f, -0.2f};
    Vec3 steam_point{0.0f, 0.1f, -1.15f};
    Vec3 jerrycan_from{1.06f, 0.38f, 1.30f};
    Vec3 jerrycan_to{0.96f, 0.30f, 1.30f};
    Vec3 press_center{0.0f, 0.60f, 0.0f};
    Vec3 press_half{1.02f, 1.00f, 2.20f};
    Vec3 engine_audio{0.0f, 0.10f, -1.55f};
    Vec3 dash_audio{0.0f, 0.35f, -0.35f};
    f32 chase_height = 1.30f;
    f32 chase_near = 5.0f;
    f32 chase_far = 7.2f;

    bool socket_set[kLayoutParts] = {};
    LayoutBox sockets[kLayoutParts];
    bool cable_boxes_set = false;
    LayoutBox cable_boxes[kLayoutCableBoxes];
};

void car_layout_parse(CarLayout& out, const Config& cfg);
const CarLayout& car_layout();
void set_car_layout(const CarLayout& layout);

}
