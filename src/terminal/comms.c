#include "terminal/comms.h"

#include <string.h>

void comms_init(Comms* comms)
{
    Comms zero = {0};
    *comms = zero;

    CommsMsg* msg = &comms->msgs[comms->count++];
    msg->from = "DISPATCH";
    msg->sub = "RELAY LINK TEST";
    msg->cycle = 1;
    msg->delivered = 1;
    msg->body =
        "THIS IS A PLACEHOLDER TRANSMISSION.\n"
        "\n"
        "IF YOU CAN READ THIS, THE RELAYNET TERMINAL IN YOUR\n"
        "TERMINAL IS RECEIVING TRAFFIC AS INTENDED. REAL MESSAGES\n"
        "AND ATTACHMENTS WILL BE WRITTEN LATER.\n"
        "\n";
    msg->att_site = "TEST RIDGE";
    msg->att_part = "RADIATOR";
    msg->att_part_cond = 62.0f;
    msg->has_briefing = 1;
    msg->briefing.speaker = "DISPATCH";
    msg->briefing.sprite_rows = COMMS_SPRITE_ROWS;
    msg->briefing.sprite[0]  = "        .-========-.          ";
    msg->briefing.sprite[1]  = "       /  ________  \\         ";
    msg->briefing.sprite[2]  = "      |  /        \\  |        ";
    msg->briefing.sprite[3]  = "      | |  .-  -.  | |        ";
    msg->briefing.sprite[4]  = "      | |  [o][o]  | |        ";
    msg->briefing.sprite[5]  = "      | |    ..    | |        ";
    msg->briefing.sprite[6]  = "      | |   ----   | |        ";
    msg->briefing.sprite[7]  = "      |  \\________/  |        ";
    msg->briefing.sprite[8]  = "      |   ___||___   |        ";
    msg->briefing.sprite[9]  = "     /|  |  HQ...  | |\\       ";
    msg->briefing.sprite[10] = "    / |  |_________| | \\      ";
    msg->briefing.sprite[11] = "   |  |   |       |  |  |     ";
    msg->briefing.sprite[12] = "   |  |___|       |__|  |     ";
    msg->briefing.sprite[13] = "   |______|       |_____|     ";
    msg->briefing.page_count = 2;
    msg->briefing.pages[0] =
        "THIS IS A RECORDED\n"
        "BRIEFING PLACEHOLDER.\n"
        "\n"
        "THE VOICE PIPELINE,\n"
        "PAGING AND PORTRAIT\n"
        "SCAN ALL RUN OFF THIS\n"
        "TEXT, SO WHEN I EVENTUALLY\n"
        "WRITE THE REAL SCRIPTS\n"
        "THEY DROP STRAIGHT IN.";
    msg->briefing.pages[1] =
        "SECOND PAGE, SAME\n"
        "DEAL.\n"
        "\n"
        "END OF PLACEHOLDER\n"
        "RECORDING. DISPATCH\n"
        "OUT.";
}

i32 comms_delivered_count(const Comms* comms)
{
    i32 n = 0;
    for (i32 i = 0; i < comms->count; i++) {
        if (comms->msgs[i].delivered) {
            n++;
        }
    }
    return n;
}

i32 comms_unread_count(const Comms* comms)
{
    i32 n = 0;
    for (i32 i = 0; i < comms->count; i++) {
        if (comms->msgs[i].delivered && !comms->msgs[i].read) {
            n++;
        }
    }
    return n;
}

CommsMsg* comms_inbox_get(Comms* comms, i32 number)
{
    i32 n = 0;
    for (i32 i = 0; i < comms->count; i++) {
        if (!comms->msgs[i].delivered) {
            continue;
        }
        n++;
        if (n == number) {
            return &comms->msgs[i];
        }
    }
    return 0;
}
