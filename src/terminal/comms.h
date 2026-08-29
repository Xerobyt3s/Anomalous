#pragma once

#include "core/types.h"

#include <string_view>

namespace anom {

inline constexpr u32 kCommsMsgMax = 24;
inline constexpr u32 kCommsSpriteRows = 14;
inline constexpr u32 kCommsPagesMax = 6;

struct CommsBriefing {
    std::string_view speaker;
    std::string_view sprite[kCommsSpriteRows];
    u32 sprite_rows = 0;
    std::string_view pages[kCommsPagesMax];
    u32 page_count = 0;
};

struct CommsMsg {
    std::string_view from;
    std::string_view sub;
    std::string_view body;
    std::string_view att_site;
    std::string_view att_part;
    f32 att_part_cond = 0.0f;
    bool has_briefing = false;
    CommsBriefing briefing;
    bool delivered = false;
    bool read = false;
    bool site_downloaded = false;
    bool part_claimed = false;
    i32 cycle = 0;

    std::string_view page(i32 index) const
    {
        if (index < 0 || static_cast<u32>(index) >= briefing.page_count) {
            return {};
        }
        return briefing.pages[index];
    }
};

class Mailbox {
public:
    void init();

    i32 delivered_count() const;
    i32 unread_count() const;

    CommsMsg* get(i32 number);
    const CommsMsg* get(i32 number) const;

private:
    CommsMsg msgs_[kCommsMsgMax];
    u32 count_ = 0;
};

} // namespace anom
