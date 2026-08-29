#pragma once

#include "core/fixed_string.h"
#include "core/types.h"

#include <string_view>

namespace anom {

inline constexpr u32 kTapeTrackMax = 96;

class TapeLibrary {
public:
    void init();

    u32 count() const { return count_; }
    std::string_view name(i32 track) const;
    std::string_view path(i32 track) const;
    bool on_relay(i32 track) const;
    u32 file_bytes(i32 track) const;
    std::string_view label(i32 aux) const;

private:
    struct Track {
        FixedString<128> path;
        FixedString<48> name;
        bool on_relay = false;
        u32 file_bytes = 0;
    };

    void scan_dir(std::string_view dir, bool on_relay);
    bool valid(i32 track) const { return track >= 0 && static_cast<u32>(track) < count_; }

    Track tracks_[kTapeTrackMax];
    u32 count_ = 0;
};

} // namespace anom
