#include "terminal/comms.h"

namespace anom {

void Mailbox::init()
{
    *this = Mailbox{};

    CommsMsg& msg = msgs_[count_++];
    msg.from = "DISPATCH";
    msg.sub = "RELAY LINK TEST";
    msg.cycle = 1;
    msg.delivered = true;
    msg.body =
        "THIS IS A PLACEHOLDER TRANSMISSION.\n"
        "\n"
        "IF YOU CAN READ THIS, THE RELAYNET TERMINAL IN YOUR\n"
        "TERMINAL IS RECEIVING TRAFFIC AS INTENDED. REAL MESSAGES\n"
        "AND ATTACHMENTS WILL BE WRITTEN LATER.\n"
        "\n";
    msg.att_site = "TEST RIDGE";
    msg.att_part = "RADIATOR";
    msg.att_part_cond = 62.0f;
    msg.has_briefing = true;

    CommsBriefing& brief = msg.briefing;
    brief.speaker = "DISPATCH";
    brief.sprite_rows = kCommsSpriteRows;
    brief.sprite[0] = R"(        .-========-.          )";
    brief.sprite[1] = R"(       /  ________  \         )";
    brief.sprite[2] = R"(      |  /        \  |        )";
    brief.sprite[3] = R"(      | |  .-  -.  | |        )";
    brief.sprite[4] = R"(      | |  [o][o]  | |        )";
    brief.sprite[5] = R"(      | |    ..    | |        )";
    brief.sprite[6] = R"(      | |   ----   | |        )";
    brief.sprite[7] = R"(      |  \________/  |        )";
    brief.sprite[8] = R"(      |   ___||___   |        )";
    brief.sprite[9] = R"(     /|  |  HQ...  | |\       )";
    brief.sprite[10] = R"(    / |  |_________| | \      )";
    brief.sprite[11] = R"(   |  |   |       |  |  |     )";
    brief.sprite[12] = R"(   |  |___|       |__|  |     )";
    brief.sprite[13] = R"(   |______|       |_____|     )";
    brief.page_count = 2;
    brief.pages[0] =
        "THIS IS A RECORDED\n"
        "BRIEFING PLACEHOLDER.\n"
        "\n"
        "THE VOICE PIPELINE,\n"
        "PAGING AND PORTRAIT\n"
        "SCAN ALL RUN OFF THIS\n"
        "TEXT, SO WHEN I EVENTUALLY\n"
        "WRITE THE REAL SCRIPTS\n"
        "THEY DROP STRAIGHT IN.";
    brief.pages[1] =
        "SECOND PAGE, SAME\n"
        "DEAL.\n"
        "\n"
        "END OF PLACEHOLDER\n"
        "RECORDING. DISPATCH\n"
        "OUT.";
}

i32 Mailbox::delivered_count() const
{
    i32 n = 0;
    for (u32 i = 0; i < count_; i++) {
        n += msgs_[i].delivered ? 1 : 0;
    }
    return n;
}

i32 Mailbox::unread_count() const
{
    i32 n = 0;
    for (u32 i = 0; i < count_; i++) {
        n += msgs_[i].delivered && !msgs_[i].read ? 1 : 0;
    }
    return n;
}

CommsMsg* Mailbox::get(i32 number)
{
    i32 n = 0;
    for (u32 i = 0; i < count_; i++) {
        if (!msgs_[i].delivered) {
            continue;
        }
        n++;
        if (n == number) {
            return &msgs_[i];
        }
    }
    return nullptr;
}

const CommsMsg* Mailbox::get(i32 number) const
{
    return const_cast<Mailbox*>(this)->get(number);
}

} // namespace anom
