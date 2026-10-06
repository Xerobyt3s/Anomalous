#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"

#include <string_view>

namespace ghost::game {
struct AmmoData;
}

namespace anom {

class CarSys;
class PhysWorld;
class Terrain;
class Vehicle;

inline constexpr u32 kTermMaxWires = 12;
inline constexpr u32 kTermMaxPoints = 36928;
inline constexpr u32 kTermMaxLineVerts = 16384;

enum PortState : i32 {
    PORT_UNPLUGGED = 0,
    PORT_PLUGGED,
    PORT_LINKED,
};

struct TermView {
    const CarSys* sys = nullptr;
    const ghost::game::AmmoData* ammo = nullptr;
    const Vehicle* veh = nullptr;
    PhysWorld* phys = nullptr;
    const Terrain* terrain = nullptr;

    f32 time_of_day = 0.0f;
    f32 weather_rain = 0.0f;
    f32 weather_wetness = 0.0f;
    i32 weather_mode = 0;

    Vec3 car_pos;
    Vec3 garage_pos;
    f32 speed_kmh = 0.0f;
    f32 rpm = 0.0f;
    f32 orbit = 0.0f;
    f32 zoom = 1.0f;

    f32 travel_charge = 0.0f;
    bool travel_ready = false;
    bool travel_primed = false;

    i32 coax_state = PORT_UNPLUGGED;
    i32 bus_state = PORT_UNPLUGGED;
    i32 antenna_tier = -1;
    bool coax_camera = false;
    bool bus_tower = false;
    bool bus_printer = false;
    bool tower_breached = false;
    Vec3 tower_pos;
};

struct TermWire {
    FixedString<32> mesh;
    Mat4 model;
    Vec3 color;
};

struct TermPoint {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 shade = 0.0f;
};

struct TermScene {
    TermWire wires[kTermMaxWires];
    u32 wire_count = 0;
    Mat4 vp3d = mat4_identity();

    const TermPoint* points = nullptr;
    u32 point_count = 0;
    bool points_dirty = false;

    const TermPoint* lines = nullptr;
    u32 line_vertex_count = 0;
    f32 point_reveal = 0.0f;
    Vec3 point_center;
    f32 sweep = 0.0f;

    i32 photo = -1;
    u32 video_texture = 0;
    f32 image_reveal = 1.0f;

    void clear()
    {
        wire_count = 0;
        points = nullptr;
        point_count = 0;
        points_dirty = false;
        lines = nullptr;
        line_vertex_count = 0;
        photo = -1;
        video_texture = 0;
        image_reveal = 1.0f;
    }

    void push_wire(std::string_view mesh, const Mat4& model, Vec3 color)
    {
        if (wire_count >= kTermMaxWires) {
            return;
        }
        wires[wire_count].mesh.assign(mesh);
        wires[wire_count].model = model;
        wires[wire_count].color = color;
        wire_count++;
    }
};

struct TermRequest {
    bool breach_open = false;
    bool travel_arm = false;
    bool travel_disarm = false;
    i32 travel_destination = -1;
    bool tower_download = false;
    bool link_port[2] = {};
    bool tape_write = false;
    i32 tape_write_value = -1;
    bool time_set = false;
    f32 time_value = 0.0f;
    i32 weather_mode = -1;
    bool power_off = false;
    bool synth_print = false;
    u8 synth_doses[3] = {};
    u32 synth_dose_count = 0;
    u32 synth_count = 0;
};

} // namespace anom
