#pragma once

#include "terminal/comms.h"
#include "terminal/mapdata.h"
#include "terminal/program.h"
#include "terminal/programs/boot.h"
#include "terminal/programs/breach.h"
#include "terminal/programs/comms.h"
#include "terminal/programs/link.h"
#include "terminal/programs/map.h"
#include "terminal/programs/status.h"
#include "terminal/programs/tapes.h"
#include "terminal/shell.h"
#include "terminal/virus.h"

namespace anom {

class Arena;
class DiskStore;
class TapeLibrary;

enum class TermMode : u32 {
    Boot,
    Shell,
    Status,
    Map,
    Comms,
    Link,
    View,
    Video,
    Breach,
    Tapes,
    Dev,
};

class Terminal {
public:
    explicit Terminal(const TapeLibrary& tapes)
        : status_(virus_), map_(mapdata_, virus_), comms_(mail_), tapes_(tapes)
    {
    }

    void init(Arena& arena, DiskStore& disks);
    void power(bool on);
    bool powered() const { return powered_; }

    void update(const TermView& view, f32 dt);

    void key_char(char c);
    void key(TermKey k);

    void set_disk(i32 disk) { shell_.set_disk(disk); }
    void set_video_texture(u32 texture) { video_.set_texture(texture); }
    bool video_active() const { return powered_ && mode_ == TermMode::Video; }

    TermMode mode() const { return mode_; }
    Screen& screen() { return shell_.screen(); }
    const Screen& screen() const { return shell_.screen(); }
    Fs& fs() { return shell_.fs(); }
    MapData& mapdata() { return mapdata_; }
    const TermScene& scene() const { return scene_; }

    TermRequest take_request();

    f32 pixelate();
    f32 virus_fx() const;

private:
    Program& program(TermMode mode);
    Program& active() { return program(mode_); }
    ProgramContext context();

    void set_mode(TermMode mode, const TermView& view);
    void launch(const FsNode& node, const TermView& view);
    void av_scan();
    void print_objective(u32 stage);

    Virus virus_;
    MapData mapdata_;
    Mailbox mail_;
    Shell shell_;

    BootProgram boot_;
    ShellProgram shell_prog_;
    StatusProgram status_;
    MapProgram map_;
    CommsProgram comms_;
    LinkProgram link_;
    ViewProgram view_prog_;
    VideoProgram video_;
    BreachProgram breach_;
    TapesProgram tapes_;
    DevProgram dev_;

    TermScene scene_;
    TermRequest request_;
    TermView last_view_;

    TermMode mode_ = TermMode::Boot;
    f32 blink_ = 0.0f;
    bool powered_ = false;
    bool wants_off_ = false;
};

} // namespace anom
