#pragma once

#include "core/fixed_string.h"
#include "terminal/fs.h"
#include "terminal/program.h"

namespace anom {

class TapeLibrary;

inline constexpr u32 kTapesListMax = 160;
inline constexpr i32 kTapesListRows = 14;
inline constexpr u32 kTapesDestMax = 40;

class TapesProgram : public Program {
public:
    explicit TapesProgram(const TapeLibrary& tapes) : tapes_(&tapes) {}

    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    bool key(const ProgramContext& ctx, const TermView& view, TermKey key) override;
    void key_char(const ProgramContext& ctx, const TermView& view, char c) override;

private:
    struct Entry {
        i32 drive = -1;
        i32 node = -1;
        i32 track = 0;
        bool archive = false;
    };

    static bool relay_linked(const TermView& view);
    i32 build_list(const ProgramContext& ctx, const TermView& view, Entry* out, i32 max) const;
    void track_file_name(i32 track, FixedString<kFsNameMax + 1>& out) const;
    void set_status(std::string_view msg);
    bool resolve_dest(const ProgramContext& ctx, i32 track);
    void finish_download(const ProgramContext& ctx);
    void submit(const ProgramContext& ctx, const TermView& view);

    void draw_list(const ProgramContext& ctx, const TermView& view);
    void draw_write(const ProgramContext& ctx);
    void draw_download(const ProgramContext& ctx);
    void progress_bar(Screen& s, i32 row, f32 frac) const;

    const TapeLibrary* tapes_ = nullptr;

    f32 timer_ = 0.0f;
    i32 sel_ = 0;
    i32 count_ = 0;

    i32 write_track_ = -1;
    f32 write_t_ = 0.0f;
    f32 click_t_ = 0.0f;

    i32 dl_track_ = -1;
    f32 dl_t_ = 0.0f;

    i32 prompt_track_ = -1;
    char dest_[kTapesDestMax] = {};
    u32 dest_len_ = 0;
    u32 dest_cursor_ = 0;
    i32 dest_drive_ = 0;
    i32 dest_node_ = -1;
    FixedString<kFsNameMax + 1> dest_name_;

    FixedString<kTapesDestMax> status_;
    f32 status_until_ = 0.0f;
};

} // namespace anom
