#include "test.h"

#include "platform/input_context.h"

using namespace anom;

TEST(input_context, nothing_active_leaves_only_global_listening)
{
    InputContext ctx;
    ctx.begin_frame();

    CHECK(ctx.keyboard(InputLayer::Global));
    CHECK(!ctx.keyboard(InputLayer::Gameplay));
    CHECK(!ctx.keyboard(InputLayer::Editor));
    CHECK(!ctx.pointer(InputLayer::Gameplay));
    CHECK(!ctx.text_entry());
    CHECK(!ctx.cursor_captured());
}

TEST(input_context, gameplay_owns_keys_and_the_cursor_when_it_is_alone)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::Gameplay);

    CHECK(ctx.keyboard(InputLayer::Gameplay));
    CHECK(ctx.keyboard(InputLayer::Global));
    CHECK(ctx.pointer(InputLayer::Gameplay));
    CHECK(ctx.cursor_captured());
    CHECK(ctx.top() == InputLayer::Gameplay);
}

TEST(input_context, a_focused_text_field_swallows_every_hotkey)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::Gameplay);
    ctx.activate(InputLayer::Editor);
    ctx.activate(InputLayer::TextField);

    CHECK(ctx.text_entry());
    CHECK(!ctx.keyboard(InputLayer::Global));
    CHECK(!ctx.keyboard(InputLayer::Editor));
    CHECK(!ctx.keyboard(InputLayer::Gameplay));
    CHECK(ctx.keyboard(InputLayer::TextField));
    CHECK(!ctx.cursor_captured());
}

TEST(input_context, the_terminal_swallows_hotkeys_below_it)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::Gameplay);
    ctx.activate(InputLayer::Terminal);

    CHECK(ctx.text_entry());
    CHECK(ctx.text(InputLayer::Terminal));
    CHECK(!ctx.keyboard(InputLayer::Gameplay));
    CHECK(!ctx.keyboard(InputLayer::Global));
    CHECK(ctx.pointer(InputLayer::Terminal));
    CHECK(!ctx.pointer(InputLayer::Gameplay));
}

TEST(input_context, a_text_field_outranks_the_terminal)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::Terminal);
    ctx.activate(InputLayer::TextField);

    CHECK(ctx.text(InputLayer::TextField));
    CHECK(!ctx.text(InputLayer::Terminal));
    CHECK(ctx.top() == InputLayer::TextField);
}

TEST(input_context, the_editor_takes_keys_from_gameplay)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::Gameplay);
    ctx.activate(InputLayer::Editor);

    CHECK(ctx.keyboard(InputLayer::Editor));
    CHECK(!ctx.keyboard(InputLayer::Gameplay));
    CHECK(ctx.keyboard(InputLayer::Global));
    CHECK(ctx.pointer(InputLayer::Editor));
    CHECK(!ctx.pointer(InputLayer::Gameplay));
}

TEST(input_context, a_hovered_panel_blocks_the_pointer_but_not_the_keyboard)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::Gameplay);
    ctx.activate(InputLayer::Editor);
    ctx.activate(InputLayer::Panels);

    CHECK(!ctx.pointer(InputLayer::Editor));
    CHECK(!ctx.pointer(InputLayer::Gameplay));
    CHECK(ctx.pointer(InputLayer::Panels));

    CHECK(ctx.keyboard(InputLayer::Editor));
    CHECK(ctx.keyboard(InputLayer::Global));
}

TEST(input_context, a_hovered_panel_releases_the_cursor)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::Gameplay);
    CHECK(ctx.cursor_captured());

    ctx.activate(InputLayer::Panels);
    CHECK(!ctx.cursor_captured());
}

TEST(input_context, begin_frame_clears_every_layer)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::TextField);
    ctx.activate(InputLayer::Editor);
    CHECK(ctx.text_entry());

    ctx.begin_frame();
    CHECK(!ctx.text_entry());
    CHECK(!ctx.active(InputLayer::Editor));
    CHECK(ctx.top() == InputLayer::Global);
}

TEST(input_context, a_layer_can_be_deactivated_explicitly)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::TextField, true);
    CHECK(ctx.text_entry());

    ctx.activate(InputLayer::TextField, false);
    CHECK(!ctx.text_entry());
    CHECK(ctx.keyboard(InputLayer::Global));
}

TEST(input_context, panels_never_claim_the_keyboard)
{
    InputContext ctx;
    ctx.begin_frame();
    ctx.activate(InputLayer::Panels);

    CHECK(!ctx.keyboard(InputLayer::Panels));
    CHECK(ctx.keyboard(InputLayer::Global));
}

TEST(input_context, every_layer_has_a_name)
{
    for (u32 i = 0; i < kInputLayerCount; i++) {
        const char* name = input_layer_name(static_cast<InputLayer>(i));
        CHECK(name != nullptr);
        CHECK(name[0] != 0);
    }
}
