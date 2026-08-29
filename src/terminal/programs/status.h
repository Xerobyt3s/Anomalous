#pragma once

#include "core/fixed_string.h"
#include "terminal/fs.h"
#include "terminal/program.h"

namespace anom {

class Virus;

class StatusProgram : public Program {
public:
    explicit StatusProgram(const Virus& virus) : virus_(&virus) {}

    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;

private:
    void build_wires(const ProgramContext& ctx, const TermView& view) const;

    const Virus* virus_ = nullptr;
    f32 spin_ = 0.0f;
};

class ViewProgram : public Program {
public:
    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;

    void set(i32 photo, std::string_view name);

private:
    i32 photo_ = -1;
    f32 materialize_ = 0.0f;
    FixedString<kFsNameMax + 1> name_;
};

class VideoProgram : public Program {
public:
    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;

    void set_texture(u32 texture) { texture_ = texture; }

private:
    u32 texture_ = 0;
    f32 materialize_ = 0.0f;
};

} // namespace anom
