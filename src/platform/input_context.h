#pragma once

#include "core/types.h"

namespace anom {

enum class InputLayer : u32 {
    TextField,
    Terminal,
    Editor,
    Panels,
    Gameplay,
    Global,
    Count,
};

inline constexpr u32 kInputLayerCount = static_cast<u32>(InputLayer::Count);

const char* input_layer_name(InputLayer layer);

class InputContext {
public:
    void begin_frame();
    void activate(InputLayer layer, bool on = true);

    bool active(InputLayer layer) const { return active_[static_cast<u32>(layer)]; }
    InputLayer top() const;

    bool keyboard(InputLayer layer) const;
    bool pointer(InputLayer layer) const;
    bool text(InputLayer layer) const;

    bool text_entry() const;
    bool cursor_captured() const;

private:
    bool active_[kInputLayerCount] = {};
};

} // namespace anom
