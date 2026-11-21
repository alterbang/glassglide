#include "browser_ui.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace glassglide;

static unsigned press(BrowserUI& ui, Action action, int value = 0)
{
    for (const auto& button : ui.buttons()) {
        if (button.action == action && button.value == value && button.enabled)
            return ui.pointer_down(button.rect.x + button.rect.width / 2.0,
                button.rect.y + button.rect.height / 2.0);
    }
    assert(false && "requested button is not available");
    return 0;
}

int main()
{
    BrowserState state;
    BrowserUI ui(state);
    assert(ui.move(Direction::Left) & Repaint);
    assert(ui.activate_focused() & LoadPage);
    assert(state.current_page() == Page::Home);

    press(ui, Action::Address);
    assert(ui.panel() == Panel::Address);
    press(ui, Action::ClearAddress);
    for (char character : std::string("https://example.com"))
        ui.type_character(character);
    unsigned blocked = press(ui, Action::SubmitAddress);
    assert(!(blocked & LoadPage) && !(blocked & Persist));
    assert(ui.notice().find("bloccati") != std::string::npos);
    assert(state.current_page() == Page::Home && state.history().empty());

    unsigned opened = press(ui, Action::OpenPage, static_cast<int>(Page::Demo));
    assert((opened & LoadPage) && (opened & Persist));
    assert(state.current_page() == Page::Demo && ui.panel() == Panel::None);
    assert(press(ui, Action::Favorite) & Persist);
    assert(state.is_favorite(Page::Demo));
    press(ui, Action::Favorites);
    assert(ui.panel() == Panel::Favorites);
    press(ui, Action::OpenPage, static_cast<int>(Page::Demo));

    auto history = state.history();
    assert(press(ui, Action::NewPrivateTab) & LoadPage);
    assert(state.current_tab().private_mode);
    press(ui, Action::Address);
    press(ui, Action::OpenPage, static_cast<int>(Page::Guide));
    assert(state.history() == history);
    for (const auto& button : ui.buttons()) {
        if (button.action == Action::Favorite)
            assert(!button.enabled);
    }
    assert(press(ui, Action::PageFocus) & Repaint);
    assert(ui.page_focused());
    assert(press(ui, Action::PageFocus) & Repaint);
    assert(!ui.page_focused());
    assert(press(ui, Action::PageFocus) & Repaint);
    assert(ui.page_focused());
    assert(ui.back_or_exit() & Repaint);
    assert(!ui.page_focused());

    press(ui, Action::History);
    assert(ui.panel() == Panel::History);
    assert(press(ui, Action::ClearHistory) & Persist);
    assert(state.history().empty());
    for (const auto& tab : state.tabs())
        assert(tab.trail.size() == 1);
    press(ui, Action::AskClearData);
    assert(ui.panel() == Panel::ConfirmClear);
    press(ui, Action::ClosePanel);
    assert(state.is_favorite(Page::Demo));
    press(ui, Action::History);
    press(ui, Action::AskClearData);
    unsigned cleared = press(ui, Action::ConfirmClearData);
    assert((cleared & LoadPage) && (cleared & Persist));
    assert(state.tabs().size() == 1 && state.favorites().empty());

    assert(ui.back_or_exit() & Exit);
    std::puts("Browser controls, blocked URLs and private data: OK");
    return 0;
}
