#include "test.h"

#include "audio/tapes.h"
#include "carsys/carsys.h"
#include "core/arena.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "terminal/disks.h"
#include "terminal/terminal.h"
#include "vehicle/vehicle.h"
#include "world/destination.h"
#include "world/terrain.h"

#include <cstring>

using namespace anom;

namespace {

constexpr f32 kDt = 1.0f / 60.0f;

struct Rig {
    Arena arena{megabytes(64)};
    DiskStore disks;
    TapeLibrary tapes;
    Terrain terrain;
    PhysWorld world;
    Vehicle car;
    CarSys sys;
    Terminal term{tapes};
    TermView view;

    bool setup()
    {
        Heightfield& hf = terrain.heightfield();
        hf.alloc(arena, 64, 8.0f);
        for (u32 z = 0; z < 64; z++) {
            for (u32 x = 0; x < 64; x++) {
                hf.set_height(x, z, 0.0f);
            }
        }
        hf.recompute_extents();
        world.init(arena, &hf);
        sys.init();
        sys.parts[PART_COMPUTER].installed = true;
        if (!car.init(world, arena, "assets/cars/excel.cfg", Vec3{0.0f, 1.0f, 0.0f}, 0.0f)) {
            return false;
        }

        disks.init(arena);
        term.init(arena, disks);
        term.mapdata().init(hf);
        term.power(true);

        view.sys = &sys;
        view.veh = &car;
        view.phys = &world;
        view.terrain = &terrain;
        view.car_pos = Vec3{0.0f, 0.0f, 0.0f};
        view.garage_pos = Vec3{20.0f, 0.0f, 20.0f};
        return true;
    }

    void tick(i32 frames = 1)
    {
        for (i32 i = 0; i < frames; i++) {
            term.update(view, kDt);
        }
    }

    void boot()
    {
        tick(320);
    }

    void type(std::string_view text)
    {
        for (const char c : text) {
            term.key_char(c);
        }
    }

    void command(std::string_view text)
    {
        type(text);
        term.key(TermKey::Enter);
        tick();
        term.screen().flush_pending();
    }

    void link_bus(bool tower)
    {
        view.bus_state = PORT_LINKED;
        view.bus_tower = tower;
    }

    void link_coax(bool camera)
    {
        view.coax_state = PORT_LINKED;
        view.coax_camera = camera;
        view.antenna_tier = camera ? -1 : 1;
    }

    bool said(std::string_view needle) const
    {
        const Screen& s = term.screen();
        for (u32 i = 0; i < s.line_count(); i++) {
            if (s.line(i).find(needle) != std::string_view::npos) {
                return true;
            }
        }
        return s.out_line().find(needle) != std::string_view::npos;
    }

    bool on_grid(std::string_view needle) const
    {
        const Screen& s = term.screen();
        char row[kTermCols + 1] = {};
        for (u32 r = 0; r < kTermRows; r++) {
            for (u32 c = 0; c < kTermCols; c++) {
                const u16 g = s.glyph(r, c);
                row[c] = g >= 32 && g < 127 ? static_cast<char>(g) : ' ';
            }
            if (std::string_view(row, kTermCols).find(needle) != std::string_view::npos) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST(terminal, boots_into_the_shell)
{
    Rig rig;
    CHECK(rig.setup());
    CHECK(rig.term.mode() == TermMode::Boot);
    rig.tick(60);
    CHECK(rig.term.mode() == TermMode::Boot);
    rig.boot();
    CHECK(rig.term.mode() == TermMode::Shell);
}

TEST(terminal, status_needs_a_vehicle_bus)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();

    rig.command("STATUS");
    CHECK(rig.term.mode() == TermMode::Shell);
    CHECK(rig.said("NO VEHICLE BUS CABLE"));

    rig.view.bus_state = PORT_PLUGGED;
    rig.command("STATUS");
    CHECK(rig.term.mode() == TermMode::Shell);
    CHECK(rig.said("BUS PORT NOT INITIALIZED"));

    rig.link_bus(false);
    rig.command("STATUS");
    CHECK(rig.term.mode() == TermMode::Status);
}

TEST(terminal, status_draws_the_diagnostics_and_a_wireframe)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.link_bus(false);
    rig.command("STATUS");
    rig.tick();

    CHECK(rig.on_grid("VEHICLE DIAGNOSTICS"));
    CHECK(rig.on_grid("ENGINE"));
    CHECK(rig.on_grid("TIRE FL"));
    CHECK(rig.term.scene().wire_count > 0);
}

TEST(terminal, status_exits_when_the_bus_drops)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.link_bus(false);
    rig.command("STATUS");
    CHECK(rig.term.mode() == TermMode::Status);

    rig.view.bus_state = PORT_UNPLUGGED;
    rig.tick();
    CHECK(rig.term.mode() == TermMode::Shell);
}

TEST(terminal, map_needs_an_antenna_on_the_coax)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();

    rig.command("MAP");
    CHECK(rig.said("NO ANTENNA FEED"));

    rig.link_coax(true);
    rig.command("MAP");
    CHECK(rig.term.mode() == TermMode::Shell);
    CHECK(rig.said("COAX FEED IS CAMERA"));

    rig.link_coax(false);
    rig.command("MAP");
    CHECK(rig.term.mode() == TermMode::Map);
}

TEST(terminal, map_builds_a_lidar_cloud_around_the_car)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.link_coax(false);
    rig.command("MAP");
    rig.tick();

    CHECK(rig.on_grid("TERRAIN SURVEY"));
    CHECK(rig.term.scene().point_count >= static_cast<u32>(kLidarN * kLidarN));
    CHECK(rig.term.scene().points != nullptr);
}

TEST(terminal, map_toggles_to_wide_range)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.link_coax(false);
    rig.command("MAP");
    rig.tick();

    rig.term.key_char('M');
    rig.tick();
    CHECK(rig.on_grid("WIDE RANGE"));
    CHECK(rig.on_grid("COVERAGE"));
}

TEST(terminal, breach_needs_a_secured_port)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();

    rig.command("BREACH");
    CHECK(rig.said("NOTHING TO BREACH"));

    rig.link_bus(true);
    rig.command("BREACH");
    CHECK(rig.term.mode() == TermMode::Breach);
    rig.tick();
    CHECK(rig.on_grid("ICE BREAK"));

    rig.view.tower_breached = true;
    rig.term.key(TermKey::Quit);
    rig.command("BREACH");
    CHECK(rig.said("PORT ALREADY OPEN"));
}

TEST(terminal, breach_aborts_when_the_port_is_pulled)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.link_bus(true);
    rig.command("BREACH");
    CHECK(rig.term.mode() == TermMode::Breach);

    rig.view.bus_state = PORT_UNPLUGGED;
    rig.tick();
    rig.term.screen().flush_pending();
    CHECK(rig.term.mode() == TermMode::Shell);
    CHECK(rig.said("BREACH ABORTED"));
}

TEST(terminal, comms_connects_then_lists_the_mailbox)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("COMMS");
    CHECK(rig.term.mode() == TermMode::Comms);

    rig.tick(45);
    CHECK(rig.on_grid("RELAYNET TERMINAL"));

    rig.tick(400);
    CHECK(rig.on_grid("MAILBOX"));
    CHECK(rig.on_grid("DISPATCH"));
}

TEST(terminal, comms_opens_a_message_and_its_briefing)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("COMMS");
    rig.tick(400);

    rig.term.key_char('1');
    rig.tick();
    CHECK(rig.on_grid("TRANSMISSION 01"));
    CHECK(rig.on_grid("RELAY LINK TEST"));
    CHECK(rig.on_grid("ATTACHMENTS:"));

    rig.term.key_char('B');
    rig.tick(200);
    CHECK(rig.on_grid("RECORDED BRIEFING"));
    CHECK(rig.on_grid("PAGE 1/2"));

    rig.term.key(TermKey::Quit);
    rig.tick();
    CHECK(rig.on_grid("TRANSMISSION 01"));

    rig.term.key(TermKey::Quit);
    rig.tick();
    CHECK(rig.on_grid("MAILBOX"));

    rig.term.key(TermKey::Quit);
    rig.tick();
    CHECK(rig.term.mode() == TermMode::Shell);
}

TEST(terminal, comms_claims_an_attachment_once)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("COMMS");
    rig.tick(400);
    rig.term.key_char('1');
    rig.tick();

    CHECK(rig.on_grid("[L] SITE COORDINATES"));
    rig.term.key_char('L');
    rig.tick();
    CHECK(rig.on_grid("(ON FILE)"));

    CHECK(rig.on_grid("[P] SUPPLY VOUCHER"));
    rig.term.key_char('P');
    rig.tick();
    CHECK(rig.on_grid("(CLAIMED)"));
}

TEST(terminal, comms_pixelates_while_it_materializes)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("COMMS");
    rig.tick(400);
    CHECK_NEAR(rig.term.pixelate(), 1.0f, 0.001f);

    rig.term.key_char('1');
    rig.tick();
    CHECK(rig.term.mode() == TermMode::Comms);
    CHECK_NEAR(rig.term.pixelate(), 1.0f, 0.001f);
}

TEST(terminal, tapes_reports_the_deck_and_the_links)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("TAPES");
    CHECK(rig.term.mode() == TermMode::Tapes);
    rig.tick();

    CHECK(rig.on_grid("TAPE ARCHIVE"));
    CHECK(rig.on_grid("DECK: NO CASSETTE"));
    CHECK(rig.on_grid("NO RELAY LINK"));
    CHECK(rig.on_grid("NO VEHICLE BUS"));

    rig.link_bus(false);
    rig.tick();
    CHECK(rig.on_grid("VEHICLE BUS ONLINE"));
}

TEST(terminal, tapes_refuses_a_write_without_a_cassette)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.link_bus(false);
    rig.command("TAPES");
    rig.tick();

    rig.term.key(TermKey::Enter);
    rig.tick();
    CHECK(rig.term.mode() == TermMode::Tapes);
}

TEST(terminal, video_needs_the_camera_on_the_coax)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();

    rig.command("VIDEO");
    CHECK(rig.said("NO VIDEO SOURCE ON COAX"));

    rig.link_coax(false);
    rig.command("VIDEO");
    CHECK(rig.said("COAX FEED IS ANTENNA"));

    rig.link_coax(true);
    rig.term.set_video_texture(7);
    rig.command("VIDEO");
    CHECK(rig.term.mode() == TermMode::Video);
    rig.tick();
    CHECK(rig.on_grid("VIDEO FEED"));
    CHECK(rig.term.scene().video_texture == 7);

    rig.view.coax_state = PORT_UNPLUGGED;
    rig.tick();
    CHECK(rig.term.mode() == TermMode::Shell);
}

TEST(terminal, link_lists_both_ports)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("LINK");
    CHECK(rig.term.mode() == TermMode::Link);
    rig.tick();
    CHECK(rig.on_grid("COAX"));
    CHECK(rig.on_grid("BUS"));
}

TEST(terminal, an_infected_program_is_cured_by_the_scanner)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();

    Fs& fs = rig.term.fs();
    rig.term.set_disk(DISK_ARCADE);
    const FsRef toy = fs.resolve(Fs::root(kFsDriveB), "DRIVEBY.EXE");
    CHECK(fs.valid(toy));
    CHECK(fs.node(toy)->infected);

    rig.command("B:\\DRIVEBY");
    rig.tick();
    CHECK(rig.term.virus_fx() > 0.5f);

    rig.term.set_disk(DISK_RESCUE);
    rig.command("B:\\AVSCAN");
    CHECK(rig.said("MEMORY RESIDENT VIRUS PURGED"));
    CHECK(rig.term.virus_fx() == 0.0f);
}

TEST(terminal, a_damaged_program_refuses_to_run)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();

    Fs& fs = rig.term.fs();
    const FsRef ref = fs.resolve(Fs::root(kFsDriveA), "LINK.EXE");
    CHECK(fs.valid(ref));
    fs.node_mut(ref)->corrupted = true;

    rig.command("LINK");
    CHECK(rig.term.mode() == TermMode::Shell);
    CHECK(rig.said("PROGRAM DAMAGED"));
}

TEST(terminal, off_asks_the_host_to_cut_power)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();

    rig.command("OFF");
    CHECK(rig.term.take_request().power_off);
    CHECK(!rig.term.take_request().power_off);
}

TEST(terminal, the_relay_drive_mounts_and_unmounts_with_the_link)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    CHECK(!rig.term.fs().mounted(kFsDriveD));

    rig.link_bus(true);
    rig.view.tower_breached = true;
    rig.tick();
    CHECK(rig.term.fs().mounted(kFsDriveD));

    rig.view.bus_state = PORT_UNPLUGGED;
    rig.tick();
    CHECK(!rig.term.fs().mounted(kFsDriveD));
}

TEST(terminal, power_cycling_rebuilds_the_filesystem)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("MKDIR SCRAP");
    CHECK(rig.term.fs().valid(rig.term.fs().resolve(Fs::root(kFsDriveA), "SCRAP")));

    rig.term.power(false);
    CHECK(!rig.term.powered());
    rig.term.power(true);
    CHECK(rig.term.mode() == TermMode::Boot);
    CHECK(!rig.term.fs().valid(rig.term.fs().resolve(Fs::root(kFsDriveA), "SCRAP")));
}

TEST(terminal, an_exposure_lands_on_the_camera_disk)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.link_coax(true);
    rig.tick();
    CHECK(rig.term.fs().mounted(kFsDriveC));

    u8* shot = rig.arena.push_array<u8>(kPhotoBytes);
    CHECK(shot != nullptr);
    for (u32 i = 0; i < kPhotoPixels; i++) {
        shot[i * 3 + 0] = static_cast<u8>(i % 256);
        shot[i * 3 + 1] = 128;
        shot[i * 3 + 2] = 64;
    }

    const u32 before = rig.disks.camera_exposures_left(&rig.term.fs());
    CHECK(before == 24);
    CHECK(rig.disks.camera_capture(&rig.term.fs(), shot));
    CHECK(rig.disks.camera_exposures_left(&rig.term.fs()) == before - 1);

    rig.command("LS C:");
    CHECK(rig.said("IMG_01"));

    const FsRef pic = rig.term.fs().resolve(Fs::root(kFsDriveC), "IMG_01.PIC");
    CHECK(rig.term.fs().valid(pic));
    CHECK(rig.term.fs().node(pic)->pic > 0);
    CHECK(rig.disks.photo(rig.term.fs().node(pic)->pic) != nullptr);
}

TEST(terminal, film_runs_out_after_twenty_four_exposures)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.link_coax(true);
    rig.tick();

    u8* shot = rig.arena.push_array<u8>(kPhotoBytes);
    CHECK(shot != nullptr);
    std::memset(shot, 90, kPhotoBytes);

    u32 taken = 0;
    while (rig.disks.camera_capture(&rig.term.fs(), shot)) {
        taken++;
        CHECK(taken <= 32);
    }
    CHECK(taken == 24);
    CHECK(rig.disks.camera_exposures_left(&rig.term.fs()) == 0);
}

TEST(terminal, view_opens_a_captured_exposure)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.link_coax(true);
    rig.tick();

    u8* shot = rig.arena.push_array<u8>(kPhotoBytes);
    CHECK(shot != nullptr);
    std::memset(shot, 200, kPhotoBytes);
    CHECK(rig.disks.camera_capture(&rig.term.fs(), shot));

    rig.command("VIEW C:\\IMG_01.PIC");
    CHECK(rig.term.mode() == TermMode::View);
    rig.tick();
    CHECK(rig.term.scene().photo > 0);
    CHECK(rig.on_grid("IMAGE VIEWER"));
    CHECK(rig.on_grid("IMG_01.PIC"));
}

TEST(terminal, the_camera_disk_unmounts_when_the_coax_is_pulled)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    CHECK(!rig.term.fs().mounted(kFsDriveC));

    rig.link_coax(true);
    rig.tick();
    CHECK(rig.term.fs().mounted(kFsDriveC));

    u8* shot = rig.arena.push_array<u8>(kPhotoBytes);
    CHECK(shot != nullptr);
    std::memset(shot, 40, kPhotoBytes);
    CHECK(rig.disks.camera_capture(&rig.term.fs(), shot));

    rig.view.coax_state = PORT_UNPLUGGED;
    rig.tick();
    CHECK(!rig.term.fs().mounted(kFsDriveC));
    CHECK(rig.disks.camera_exposures_left(nullptr) == 23);

    rig.link_coax(true);
    rig.tick();
    CHECK(rig.term.fs().valid(rig.term.fs().resolve(Fs::root(kFsDriveC), "IMG_01.PIC")));
}

static f32 scene_height(const TermScene& scene)
{
    f32 lo = 1e30f;
    f32 hi = -1e30f;
    for (u32 i = 0; i < scene.line_vertex_count; i++) {
        lo = f_min(lo, scene.lines[i].y);
        hi = f_max(hi, scene.lines[i].y);
    }
    return scene.line_vertex_count > 0 ? hi - lo : 0.0f;
}

TEST(travel, the_plotter_lists_destinations_and_refuses_the_current_position)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();

    rig.command("TRAVEL");
    CHECK(rig.term.mode() == TermMode::Travel);
    rig.tick();

    CHECK(rig.on_grid("TRANSIT PLOTTER"));
    CHECK(rig.on_grid("REDLINE FLATS"));
    CHECK(rig.on_grid("KAMIYAMA PASS"));
    CHECK(rig.on_grid("UNSURVEYED"));
    CHECK(rig.on_grid("COIL NOT INSTALLED"));

    rig.term.key(TermKey::Enter);
    rig.tick();
    CHECK(rig.on_grid("TRANSIT PLOTTER"));
}

TEST(travel, plotting_a_course_folds_a_sheet_then_opens_a_throat)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("TRAVEL");

    rig.term.key(TermKey::Down);
    rig.term.key(TermKey::Enter);
    rig.tick();
    CHECK(rig.on_grid("WORMHOLE PLOT"));
    CHECK(rig.on_grid("KAMIYAMA PASS"));
    CHECK(rig.term.scene().line_vertex_count > 0);
    CHECK(rig.term.scene().lines != nullptr);

    // The funnels are a warp of the grid, not extra geometry, so the vertex count holds
    // steady while the shape changes; the fold is what makes the sheet deep.
    const u32 flat_lines = rig.term.scene().line_vertex_count;
    const f32 flat_depth = scene_height(rig.term.scene());

    rig.tick(240);
    CHECK(rig.on_grid("THROAT"));
    CHECK(rig.term.scene().line_vertex_count == flat_lines);
    CHECK(scene_height(rig.term.scene()) > flat_depth * 3.0f);
}

TEST(travel, the_plot_hands_over_to_the_spool_gauge)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("TRAVEL");
    rig.term.key(TermKey::Down);
    rig.term.key(TermKey::Enter);

    rig.view.travel_ready = true;
    rig.view.travel_charge = 0.4f;
    rig.view.speed_kmh = 104.0f;
    rig.tick(460);

    CHECK(rig.on_grid("COIL SPOOL"));
    CHECK(rig.on_grid("CHARGE"));
    CHECK(rig.on_grid("40%"));
    CHECK(rig.on_grid("104 KM/H"));
    CHECK(rig.term.scene().line_vertex_count > 0);
}

TEST(travel, a_full_coil_will_not_fire_at_an_unsurveyed_destination)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("TRAVEL");
    rig.term.key(TermKey::Down);
    rig.term.key(TermKey::Down);
    rig.term.key(TermKey::Enter);

    rig.view.travel_ready = true;
    rig.view.travel_charge = 1.0f;
    rig.tick(460);
    CHECK(rig.on_grid("EXIT POINT UNRESOLVED"));

    rig.term.key(TermKey::Enter);
    rig.tick();
    CHECK(!rig.term.take_request().travel_arm);
}

TEST(travel, the_coil_primes_at_a_surveyed_destination_before_it_is_charged)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("TRAVEL");
    rig.term.key(TermKey::Down);
    rig.term.key(TermKey::Enter);

    // Flat coil, standing still: priming has to work here, that is the whole point of it.
    rig.view.travel_ready = true;
    rig.view.travel_charge = 0.0f;
    rig.view.speed_kmh = 0.0f;
    rig.tick(460);

    rig.term.key(TermKey::Enter);
    rig.tick();
    const TermRequest req = rig.term.take_request();
    CHECK(req.travel_arm);
    CHECK(req.travel_destination == destination_index("touge"));
    CHECK(destinations()[static_cast<u32>(req.travel_destination)].surveyed());
}

TEST(travel, priming_shows_on_the_gauge_and_can_be_made_safe_again)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("TRAVEL");
    rig.term.key(TermKey::Down);
    rig.term.key(TermKey::Enter);

    rig.view.travel_ready = true;
    rig.tick(460);
    CHECK(rig.on_grid("PRIME"));

    rig.view.travel_primed = true;
    rig.tick(30);
    CHECK(rig.on_grid("FULL CHARGE. DRIVE."));

    rig.term.key(TermKey::Enter);
    rig.tick();
    const TermRequest req = rig.term.take_request();
    CHECK(req.travel_disarm);
    CHECK(!req.travel_arm);
}

TEST(travel, standing_down_returns_to_the_destination_list)
{
    Rig rig;
    CHECK(rig.setup());
    rig.boot();
    rig.command("TRAVEL");
    rig.term.key(TermKey::Down);
    rig.term.key(TermKey::Enter);
    rig.tick(460);
    CHECK(rig.on_grid("COIL SPOOL"));

    rig.term.key(TermKey::Quit);
    rig.tick();
    CHECK(rig.on_grid("TRANSIT PLOTTER"));
    CHECK(rig.term.mode() == TermMode::Travel);

    rig.term.key(TermKey::Quit);
    CHECK(rig.term.mode() == TermMode::Shell);
}

TEST(terminal, synth_needs_a_bus_link_to_the_car_or_the_printer)
{
    Rig rig;
    if (!rig.setup()) {
        FAIL("rig");
        return;
    }
    rig.boot();
    rig.sys.parts[PART_PRINTER].installed = true;
    rig.sys.parts[PART_TANK].installed = true;
    rig.command("synth");
    CHECK(rig.said("NO BUS LINK"));
    CHECK(rig.term.mode() != TermMode::Synth);

    rig.view.bus_state = PORT_LINKED;
    rig.view.bus_printer = true;
    rig.command("synth");
    CHECK(rig.term.mode() == TermMode::Synth);

    rig.view.bus_state = PORT_UNPLUGGED;
    rig.tick(2);
    rig.term.screen().flush_pending();
    CHECK(rig.term.mode() == TermMode::Shell);
    CHECK(rig.said("SYNTH LINK LOST"));

    rig.view.bus_state = PORT_LINKED;
    rig.view.bus_printer = false;
    rig.sys.parts[PART_TANK].installed = false;
    rig.command("synth");
    CHECK(rig.said("NO MATERIAL TANK ON THE PRINTER"));
    CHECK(rig.term.mode() != TermMode::Synth);

    rig.view.bus_printer = true;
    rig.command("status");
    CHECK(rig.said("BUS FEED IS PRINTER"));
}
