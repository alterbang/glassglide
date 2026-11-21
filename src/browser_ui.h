#pragma once

#include "browser_state.h"

#include <string>
#include <vector>

namespace glassglide {

enum class Panel { None, Address, Favorites, History, ConfirmClear };
enum class Direction { Left, Right, Up, Down };
enum class Action {
    Tab, NewTab, NewPrivateTab, CloseTab, Back, Forward, Reload, Home,
    Address, Favorite, Favorites, History, PageFocus, Exit, OpenPage, HistoryEntry,
    Character, EraseCharacter, ClearAddress, SubmitAddress, ClosePanel,
    HistoryPrevious, HistoryNext, ClearHistory, AskClearData, ConfirmClearData
};

struct Rect {
    int x;
    int y;
    int width;
    int height;
    bool contains(double px, double py) const;
};

struct Button {
    Action action;
    int value;
    Rect rect;
    std::string label;
    bool enabled;
    bool active;
    bool focused;
};

enum Effect : unsigned {
    Repaint = 1 << 0,
    LoadPage = 1 << 1,
    Persist = 1 << 2,
    Exit = 1 << 3,
};

class BrowserUI {
public:
    explicit BrowserUI(BrowserState& state);

    std::vector<Button> buttons() const;
    Panel panel() const { return panel_; }
    bool page_focused() const { return page_focused_; }
    const std::string& address_input() const { return address_input_; }
    const std::string& notice() const { return notice_; }
    int overlay_height() const;

    void set_notice(std::string message);
    unsigned navigate(Page page);
    unsigned move(Direction direction);
    unsigned activate_focused();
    unsigned pointer_down(double x, double y);
    unsigned back_or_exit();
    unsigned type_character(char character);
    unsigned erase_character();

private:
    unsigned activate(Action action, int value);
    void set_focus(Action action, int value = 0);

    BrowserState& state_;
    Panel panel_ = Panel::None;
    Action focus_action_ = Action::Address;
    int focus_value_ = 0;
    bool page_focused_ = false;
    std::size_t history_offset_ = 0;
    std::string address_input_;
    std::string notice_;
};

} // namespace glassglide
