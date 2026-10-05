#include "terminal/terminal.h"
#include "audio/tapes.h"
#include "core/arena.h"
#include "terminal/disks.h"
#include "world/terrain.h"

#include <cmath>

namespace anom {
void Terminal::init(Arena& arena, DiskStore& disks)
{
    mail_.init();
    shell_.init(&disks);
    map_.init(arena);
    travel_.init(arena);
}

void Terminal::power(bool on)
{
    virus_.cure();
    shell_.reset();
    scene_.clear();
    request_ = TermRequest{};
    blink_ = 0.0f;
    wants_off_ = false;
    powered_ = on;
    mode_ = TermMode::Boot;
    if (on) {
        boot_.enter(context(), last_view_);
    }
}

Program& Terminal::program(TermMode mode)
{
    switch (mode) {
    case TermMode::Boot:
        return boot_;
    case TermMode::Status:
        return status_;
    case TermMode::Map:
        return map_;
    case TermMode::Comms:
        return comms_;
    case TermMode::Link:
        return link_;
    case TermMode::View:
        return view_prog_;
    case TermMode::Video:
        return video_;
    case TermMode::Breach:
        return breach_;
    case TermMode::Tapes:
        return tapes_;
    case TermMode::Dev:
        return dev_;
    case TermMode::Travel:
        return travel_;
    case TermMode::Shell:
        break;
    }
    return shell_prog_;
}

ProgramContext Terminal::context()
{
    return ProgramContext{&shell_, &shell_.screen(), &scene_, &request_, blink_};
}

void Terminal::set_mode(TermMode mode, const TermView& view)
{
    if (mode_ != mode) {
        active().exit(context());
    }
    mode_ = mode;
    active().enter(context(), view);
}

TermRequest Terminal::take_request()
{
    const TermRequest out = request_;
    request_ = TermRequest{};
    return out;
}

void Terminal::mirror(TermMirror& out)
{
    out.screen = shell_.screen();
    out.mode = mode_;
    out.powered = powered_;
    out.pixelate = pixelate();
    out.virus = virus_fx();
    out.wire_count = scene_.wire_count;
    for (u32 i = 0; i < scene_.wire_count && i < kTermMaxWires; i++) {
        out.wires[i] = scene_.wires[i];
    }
    out.vp3d = scene_.vp3d;
    out.sweep = scene_.sweep;
    out.image_reveal = scene_.image_reveal;
    out.photo = scene_.photo;
}

void Terminal::adopt(const TermMirror& in)
{
    mirrored_ = true;
    shell_.screen() = in.screen;
    mode_ = in.mode;
    powered_ = in.powered;
    mirror_pixelate_ = in.pixelate;
    mirror_virus_ = in.virus;
    scene_.clear();
    scene_.wire_count = in.wire_count < kTermMaxWires ? in.wire_count : kTermMaxWires;
    for (u32 i = 0; i < scene_.wire_count; i++) {
        scene_.wires[i] = in.wires[i];
    }
    scene_.vp3d = in.vp3d;
    scene_.sweep = in.sweep;
    scene_.image_reveal = in.image_reveal;
    scene_.photo = in.photo;
}

f32 Terminal::pixelate()
{
    if (mirrored_) {
        return mirror_pixelate_;
    }
    if (!powered_) {
        return 1.0f;
    }
    if (virus_.active() && virus_.bursting() && std::fmod(blink_ * 13.0f, 1.0f) < 0.5f) {
        return 3.0f;
    }
    return program(mode_).pixelate();
}

f32 Terminal::virus_fx() const
{
    if (mirrored_) {
        return mirror_virus_;
    }
    return powered_ && virus_.active() && virus_.bursting() ? 1.0f : 0.0f;
}

void Terminal::av_scan()
{
    Screen& s = shell_.screen();
    Fs& fs = shell_.fs();

    u32 scanned = 0;
    u32 cleaned = 0;
    u32 damaged = 0;
    s.print("RC ANTIVIRUS 4.0 (C) ROTCLIFF COMPUTING\n");
    for (i32 drive = 0; drive < kFsDriveCount; drive++) {
        if (!fs.mounted(drive)) {
            continue;
        }
        s.printf("SCANNING DRIVE %c: ...\n", 'A' + drive);
        FsDrive& d = fs.drive(drive);
        for (i32 i = 1; i < kFsDriveNodes; i++) {
            FsNode& n = d.nodes[i];
            if (!n.used || n.is_dir) {
                continue;
            }
            scanned++;
            if (n.infected) {
                n.infected = false;
                cleaned++;
                s.printf("  %s -- INFECTED. CLEANED.\n", n.name.c_str());
            }
            if (n.corrupted) {
                damaged++;
                s.printf("  %s -- DAMAGED. CANNOT REPAIR.\n", n.name.c_str());
            }
        }
    }
    s.printf("%u FILES SCANNED. %u CLEANED. %u DAMAGED.\n", scanned, cleaned, damaged);
    if (virus_.active()) {
        virus_.cure();
        s.print("MEMORY RESIDENT VIRUS PURGED.\nSYSTEM CLEAN.\n");
    } else {
        s.print("NO RESIDENT THREATS.\n");
    }
}

void Terminal::launch(const FsNode& node, const TermView& view)
{
    Screen& s = shell_.screen();
    if (node.corrupted) {
        s.print("PROGRAM DAMAGED. CANNOT EXECUTE.\n");
        return;
    }

    switch (node.exe) {
    case FsExe::Status:
        if (view.bus_tower) {
            s.print("BUS FEED IS RELAY NODE. NO VEHICLE DIAGNOSTICS.\n");
        } else if (view.bus_state != PORT_LINKED) {
            s.print(view.bus_state == PORT_PLUGGED ? "BUS PORT NOT INITIALIZED. RUN LINK.\n"
                                                   : "NO VEHICLE BUS CABLE.\n");
        } else {
            set_mode(TermMode::Status, view);
        }
        break;

    case FsExe::Map:
        if (view.coax_state != PORT_LINKED || view.antenna_tier < 0) {
            if (view.coax_state == PORT_LINKED && view.coax_camera) {
                s.print("COAX FEED IS CAMERA. NO SURVEY ANTENNA.\n");
            } else {
                s.print(view.coax_state == PORT_PLUGGED
                            ? "COAX PORT NOT INITIALIZED. RUN LINK.\n"
                            : "NO ANTENNA FEED.\n");
            }
        } else {
            set_mode(TermMode::Map, view);
        }
        break;

    case FsExe::Link:
        set_mode(TermMode::Link, view);
        break;

    case FsExe::Comms:
        set_mode(TermMode::Comms, view);
        break;

    case FsExe::Av:
        av_scan();
        break;

    case FsExe::Video:
        if (view.coax_state == PORT_LINKED && view.coax_camera) {
            set_mode(TermMode::Video, view);
        } else if (view.coax_state == PORT_PLUGGED) {
            s.print("COAX PORT NOT INITIALIZED. RUN LINK.\n");
        } else if (view.coax_state == PORT_LINKED) {
            s.print("COAX FEED IS ANTENNA. NO VIDEO SOURCE.\n");
        } else {
            s.print("NO VIDEO SOURCE ON COAX.\n");
        }
        break;

    case FsExe::Toy:
        s.printf("%.*s\n", static_cast<int>(node.run_text.size()),
                 node.run_text.empty() ? "OUT OF MEMORY" : node.run_text.data());
        break;

    case FsExe::Breach:
        if (!(view.bus_state >= PORT_PLUGGED && view.bus_tower)) {
            s.print("NO SECURED PORT ON BUS. NOTHING TO BREACH.\n");
        } else if (view.tower_breached) {
            s.print("PORT ALREADY OPEN.\n");
        } else {
            set_mode(TermMode::Breach, view);
        }
        break;

    case FsExe::Gate:
        s.print("GATE ACTUATOR: NO BARRIER WIRED TO THIS NODE.\n");
        break;

    case FsExe::Dev:
        set_mode(TermMode::Dev, view);
        break;

    case FsExe::Tapes:
        set_mode(TermMode::Tapes, view);
        break;

    case FsExe::Travel:
        set_mode(TermMode::Travel, view);
        break;

    case FsExe::None:
        s.print("PROGRAM DAMAGED. CANNOT EXECUTE.\n");
        break;
    }

    if (node.infected) {
        virus_.infect();
    }
}

void Terminal::update(const TermView& view, f32 dt)
{
    if (!powered_) {
        return;
    }
    last_view_ = view;

    shell_.set_camera_mounted(view.coax_state == PORT_LINKED && view.coax_camera);
    shell_.set_tower_linked(view.bus_state == PORT_LINKED && view.bus_tower
                            && view.tower_breached);
    if (!(view.bus_tower && view.tower_breached)) {
        map_.clear_download_done();
    }

    Screen& s = shell_.screen();
    if (mode_ == TermMode::Status && (view.bus_state != PORT_LINKED || view.bus_tower)) {
        set_mode(TermMode::Shell, view);
    }
    if (mode_ == TermMode::Map && (view.coax_state != PORT_LINKED || view.antenna_tier < 0)) {
        set_mode(TermMode::Shell, view);
    }
    if (mode_ == TermMode::Video && (view.coax_state != PORT_LINKED || !view.coax_camera)) {
        set_mode(TermMode::Shell, view);
        s.print("VIDEO SOURCE LOST.\n");
    }
    if (mode_ == TermMode::Breach && !(view.bus_state >= PORT_PLUGGED && view.bus_tower)) {
        set_mode(TermMode::Shell, view);
        s.print("PORT CONNECTION LOST. BREACH ABORTED.\n");
    }

    blink_ += dt;
    if (blink_ > 100.0f) {
        blink_ -= 100.0f;
    }
    s.tick_blink(dt);

    if (mode_ == TermMode::Boot && boot_.done()) {
        set_mode(TermMode::Shell, view);
    }

    s.reveal(dt);
    scene_.clear();
    active().update(context(), view, dt);
    if (active().wants_exit()) {
        set_mode(TermMode::Shell, view);
    }

    const ShellRequest req = shell_.take_request();
    if (req.power_off) {
        wants_off_ = true;
        request_.power_off = true;
    }
    if (req.view_pic > 0) {
        view_prog_.set(req.view_pic, req.view_name.view());
        set_mode(TermMode::View, view);
    }
    if (req.launch_ref.node >= 0 && shell_.fs().valid(req.launch_ref)) {
        launch(*shell_.fs().node(req.launch_ref), view);
    }

    virus_.tick(s, shell_.fs(), dt, mode_ != TermMode::Boot, mode_ == TermMode::Shell);
}

void Terminal::key_char(char c)
{
    if (!powered_ || c < 32 || c > 126) {
        return;
    }
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    active().key_char(context(), last_view_, c);
}

void Terminal::key(TermKey k)
{
    if (!powered_) {
        return;
    }
    if (active().key(context(), last_view_, k)) {
        set_mode(TermMode::Shell, last_view_);
    }
}

}
