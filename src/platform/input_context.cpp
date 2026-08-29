#include "platform/input_context.h"

namespace anom {
namespace {

bool is_text_entry(InputLayer layer)
{
    return layer == InputLayer::TextField || layer == InputLayer::Terminal;
}

} // namespace

const char* input_layer_name(InputLayer layer)
{
    switch (layer) {
    case InputLayer::TextField:
        return "text field";
    case InputLayer::Terminal:
        return "terminal";
    case InputLayer::Editor:
        return "editor";
    case InputLayer::Panels:
        return "panels";
    case InputLayer::Gameplay:
        return "gameplay";
    default:
        return "global";
    }
}

void InputContext::begin_frame()
{
    for (bool& flag : active_) {
        flag = false;
    }
}

void InputContext::activate(InputLayer layer, bool on)
{
    active_[static_cast<u32>(layer)] = on;
}

InputLayer InputContext::top() const
{
    for (u32 i = 0; i < kInputLayerCount; i++) {
        if (active_[i]) {
            return static_cast<InputLayer>(i);
        }
    }
    return InputLayer::Global;
}

bool InputContext::text_entry() const
{
    return active(InputLayer::TextField) || active(InputLayer::Terminal);
}

bool InputContext::text(InputLayer layer) const
{
    if (!is_text_entry(layer) || !active(layer)) {
        return false;
    }
    return layer == InputLayer::TextField || !active(InputLayer::TextField);
}

bool InputContext::keyboard(InputLayer layer) const
{
    if (text_entry()) {
        return text(layer);
    }
    switch (layer) {
    case InputLayer::Global:
        return true;
    case InputLayer::Editor:
        return active(InputLayer::Editor);
    case InputLayer::Gameplay:
        return active(InputLayer::Gameplay) && !active(InputLayer::Editor);
    default:
        return false;
    }
}

bool InputContext::pointer(InputLayer layer) const
{
    if (active(InputLayer::Terminal)) {
        return layer == InputLayer::Terminal;
    }
    if (active(InputLayer::Panels)) {
        return layer == InputLayer::Panels;
    }
    switch (layer) {
    case InputLayer::Editor:
        return active(InputLayer::Editor);
    case InputLayer::Gameplay:
        return active(InputLayer::Gameplay) && !active(InputLayer::Editor);
    default:
        return false;
    }
}

bool InputContext::cursor_captured() const
{
    return pointer(InputLayer::Gameplay) && !text_entry();
}

} // namespace anom
