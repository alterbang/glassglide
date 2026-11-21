#include "browser_ui.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace glassglide {

bool Rect::contains(double px, double py) const
{
    return px >= x && py >= y && px < x + width && py < y + height;
}

BrowserUI::BrowserUI(BrowserState& state)
    : state_(state)
    , notice_("Anteprima locale: siti esterni disattivati")
{
}

void BrowserUI::set_notice(std::string message)
{
    notice_ = std::move(message);
}

void BrowserUI::set_focus(Action action, int value)
{
    focus_action_ = action;
    focus_value_ = value;
}

int BrowserUI::overlay_height() const
{
    return panel_ == Panel::None ? 140 : 670;
}

std::vector<Button> BrowserUI::buttons() const
{
    std::vector<Button> result;
    auto add = [&](Action action, int value, Rect rect, std::string label,
                   bool enabled = true, bool active = false) {
        bool focused = !page_focused_ && action == focus_action_ && value == focus_value_ && enabled;
        result.push_back({ action, value, rect, std::move(label), enabled, active, focused });
    };
    bool toolbar = panel_ == Panel::None;
    int tab_x = 180;
    for (std::size_t index = 0; index < state_.tabs().size(); ++index) {
        const auto& tab = state_.tabs()[index];
        std::string title = tab.private_mode ? "Privata " : "Scheda ";
        title += std::to_string(index + 1);
        add(Action::Tab, static_cast<int>(index), { tab_x, 10, 106, 37 },
            title, toolbar, index == state_.active_tab());
        tab_x += 111;
    }
    add(Action::NewTab, 0, { tab_x, 10, 87, 37 }, "+ Scheda", toolbar);
    tab_x += 92;
    add(Action::NewPrivateTab, 0, { tab_x, 10, 96, 37 }, "+ Privata", toolbar);
    tab_x += 101;
    add(Action::CloseTab, 0, { tab_x, 10, 72, 37 }, "Chiudi", toolbar);

    add(Action::Back, 0, { 24, 58, 68, 42 }, "<", toolbar && state_.can_back());
    add(Action::Forward, 0, { 97, 58, 68, 42 }, ">", toolbar && state_.can_forward());
    add(Action::Reload, 0, { 170, 58, 95, 42 }, "Ricarica", toolbar);
    add(Action::Home, 0, { 270, 58, 78, 42 }, "Home", toolbar);
    add(Action::Address, 0, { 353, 58, 396, 42 },
        page_info(state_.current_page()).address, toolbar);
    add(Action::Favorite, 0, { 754, 58, 64, 42 },
        state_.is_favorite(state_.current_page()) ? "*" : "+*",
        toolbar && !state_.current_tab().private_mode);
    add(Action::Favorites, 0, { 823, 58, 113, 42 }, "Preferiti", toolbar);
    add(Action::History, 0, { 941, 58, 116, 42 }, "Cronologia", toolbar);
    add(Action::PageFocus, 0, { 1062, 58, 88, 42 }, "Pagina", toolbar, page_focused_);
    add(Action::Exit, 0, { 1155, 58, 98, 42 }, "Esci", toolbar);

    if (panel_ == Panel::Address) {
        int x = 186;
        for (const auto& page : pages()) {
            add(Action::OpenPage, static_cast<int>(page.id), { x, 246, 190, 39 }, page.title);
            x += 196;
        }
        static const char* rows[] = {
            "abcdefghi", "jklmnopqr", "stuvwxyz", "0123456789", ":/.?&=_-"
        };
        for (int row = 0; row < 5; ++row) {
            x = 186;
            for (const char* character = rows[row]; *character; ++character) {
                add(Action::Character, static_cast<unsigned char>(*character),
                    { x, 298 + row * 47, 67, 39 }, std::string(1, *character));
                x += 72;
            }
        }
        add(Action::EraseCharacter, 0, { 186, 542, 150, 43 }, "Cancella");
        add(Action::ClearAddress, 0, { 342, 542, 150, 43 }, "Svuota");
        add(Action::SubmitAddress, 0, { 498, 542, 150, 43 }, "Vai");
        add(Action::ClosePanel, 0, { 654, 542, 150, 43 }, "Annulla");
    } else if (panel_ == Panel::Favorites) {
        int y = 206;
        for (Page page : state_.favorites()) {
            add(Action::OpenPage, static_cast<int>(page), { 186, y, 800, 42 },
                page_info(page).title);
            y += 49;
        }
        add(Action::ClosePanel, 0, { 186, 575, 150, 43 }, "Chiudi");
    } else if (panel_ == Panel::History) {
        constexpr std::size_t page_size = 8;
        std::size_t end = std::min(history_offset_ + page_size, state_.history().size());
        int y = 205;
        for (std::size_t index = history_offset_; index < end; ++index) {
            Page page = state_.history()[index];
            add(Action::HistoryEntry, static_cast<int>(index), { 186, y, 800, 38 },
                page_info(page).title);
            y += 43;
        }
        add(Action::HistoryPrevious, 0, { 186, 557, 135, 42 }, "Precedenti", history_offset_ > 0);
        add(Action::HistoryNext, 0, { 326, 557, 135, 42 }, "Successive", end < state_.history().size());
        add(Action::ClearHistory, 0, { 466, 557, 190, 42 }, "Cancella cronologia",
            !state_.history().empty());
        add(Action::AskClearData, 0, { 661, 557, 170, 42 }, "Cancella dati");
        add(Action::ClosePanel, 0, { 836, 557, 150, 42 }, "Chiudi");
    } else if (panel_ == Panel::ConfirmClear) {
        add(Action::ConfirmClearData, 0, { 186, 270, 230, 46 }, "Cancella tutto");
        add(Action::ClosePanel, 0, { 425, 270, 180, 46 }, "Annulla");
    }
    return result;
}

unsigned BrowserUI::navigate(Page page)
{
    state_.navigate(page);
    panel_ = Panel::None;
    set_focus(Action::Address);
    notice_ = "Pagina locale: " + std::string(page_info(page).title);
    return Repaint | LoadPage | Persist;
}

unsigned BrowserUI::move(Direction direction)
{
    if (page_focused_)
        return 0;
    const auto list = buttons();
    const Button* current = nullptr;
    for (const auto& button : list) {
        if (button.focused) {
            current = &button;
            break;
        }
    }
    if (!current) {
        auto first = std::find_if(list.begin(), list.end(),
            [](const Button& button) { return button.enabled; });
        if (first == list.end())
            return 0;
        set_focus(first->action, first->value);
        return Repaint;
    }

    double x = current->rect.x + current->rect.width / 2.0;
    double y = current->rect.y + current->rect.height / 2.0;
    const Button* best = nullptr;
    double best_score = std::numeric_limits<double>::max();
    for (const auto& candidate : list) {
        if (!candidate.enabled || &candidate == current)
            continue;
        double dx = candidate.rect.x + candidate.rect.width / 2.0 - x;
        double dy = candidate.rect.y + candidate.rect.height / 2.0 - y;
        double primary = direction == Direction::Left ? -dx
            : direction == Direction::Right ? dx
            : direction == Direction::Up ? -dy : dy;
        double cross = direction == Direction::Left || direction == Direction::Right
            ? std::abs(dy) : std::abs(dx);
        if (primary <= 0)
            continue;
        bool horizontal = direction == Direction::Left || direction == Direction::Right;
        bool aligned = horizontal
            ? candidate.rect.y < current->rect.y + current->rect.height
                && current->rect.y < candidate.rect.y + candidate.rect.height
            : candidate.rect.x < current->rect.x + current->rect.width
                && current->rect.x < candidate.rect.x + candidate.rect.width;
        double score = primary + 3 * cross + (aligned ? 0 : 10000);
        if (score < best_score) {
            best_score = score;
            best = &candidate;
        }
    }
    if (!best)
        return 0;
    set_focus(best->action, best->value);
    return Repaint;
}

unsigned BrowserUI::activate_focused()
{
    for (const auto& button : buttons()) {
        if (button.focused)
            return activate(button.action, button.value);
    }
    return 0;
}

unsigned BrowserUI::pointer_down(double x, double y)
{
    for (const auto& button : buttons()) {
        if (button.enabled && button.rect.contains(x, y)) {
            if (button.action != Action::PageFocus)
                page_focused_ = false;
            set_focus(button.action, button.value);
            return activate(button.action, button.value) | Repaint;
        }
    }
    return 0;
}

unsigned BrowserUI::back_or_exit()
{
    if (panel_ != Panel::None) {
        panel_ = Panel::None;
        set_focus(Action::Address);
        return Repaint;
    }
    if (page_focused_) {
        page_focused_ = false;
        set_focus(Action::PageFocus);
        notice_ = "Controlli attivi: usa le frecce e OK";
        return Repaint;
    }
    if (state_.back()) {
        notice_ = "Pagina precedente";
        return Repaint | LoadPage | Persist;
    }
    return Exit;
}

unsigned BrowserUI::type_character(char character)
{
    if (panel_ != Panel::Address || character < 32 || character > 126)
        return 0;
    if (address_input_.size() == 256) {
        notice_ = "Indirizzo troppo lungo";
        return Repaint;
    }
    address_input_ += character;
    return Repaint;
}

unsigned BrowserUI::erase_character()
{
    if (panel_ != Panel::Address || address_input_.empty())
        return 0;
    address_input_.pop_back();
    return Repaint;
}

unsigned BrowserUI::activate(Action action, int value)
{
    switch (action) {
    case Action::Tab:
        if (state_.select_tab(static_cast<std::size_t>(value))) {
            notice_ = state_.current_tab().private_mode
                ? "Scheda privata: non viene salvata"
                : "Scheda ripristinata: la pagina viene ricaricata";
            return Repaint | LoadPage | Persist;
        }
        return Repaint;
    case Action::NewTab:
    case Action::NewPrivateTab:
        if (!state_.new_tab(action == Action::NewPrivateTab)) {
            notice_ = "Massimo 6 schede aperte";
            return Repaint;
        }
        set_focus(Action::Address);
        notice_ = state_.current_tab().private_mode
            ? "Scheda privata: non viene salvata" : "Nuova scheda locale";
        return Repaint | LoadPage | Persist;
    case Action::CloseTab:
        if (!state_.close_tab(state_.active_tab())) {
            notice_ = "Deve restare aperta almeno una scheda";
            return Repaint;
        }
        set_focus(Action::Address);
        notice_ = "Scheda chiusa";
        return Repaint | LoadPage | Persist;
    case Action::Back:
        if (state_.back())
            return Repaint | LoadPage | Persist;
        return Repaint;
    case Action::Forward:
        if (state_.forward())
            return Repaint | LoadPage | Persist;
        return Repaint;
    case Action::Reload:
        notice_ = "Pagina ricaricata";
        return Repaint | LoadPage;
    case Action::Home:
        return navigate(Page::Home);
    case Action::Address:
        panel_ = Panel::Address;
        address_input_ = page_info(state_.current_page()).address;
        set_focus(Action::OpenPage, static_cast<int>(Page::Home));
        notice_ = "Scegli una pagina locale o inserisci un indirizzo";
        return Repaint;
    case Action::Favorite:
        if (state_.toggle_favorite()) {
            notice_ = state_.is_favorite(state_.current_page())
                ? "Pagina aggiunta ai preferiti" : "Pagina rimossa dai preferiti";
            return Repaint | Persist;
        }
        notice_ = "I preferiti non vengono modificati in una scheda privata";
        return Repaint;
    case Action::Favorites:
        panel_ = Panel::Favorites;
        set_focus(state_.favorites().empty() ? Action::ClosePanel : Action::OpenPage,
            state_.favorites().empty() ? 0 : static_cast<int>(state_.favorites().front()));
        return Repaint;
    case Action::History:
        panel_ = Panel::History;
        history_offset_ = 0;
        set_focus(state_.history().empty() ? Action::AskClearData : Action::HistoryEntry);
        return Repaint;
    case Action::PageFocus:
        page_focused_ = !page_focused_;
        notice_ = page_focused_
            ? "Pagina attiva: seleziona Pagina per tornare ai controlli"
            : "Controlli attivi: usa le frecce e OK";
        return Repaint;
    case Action::Exit:
        return Exit;
    case Action::OpenPage:
        if (value >= 0 && value < static_cast<int>(pages().size()))
            return navigate(static_cast<Page>(value));
        return Repaint;
    case Action::HistoryEntry:
        if (value >= 0 && static_cast<std::size_t>(value) < state_.history().size())
            return navigate(state_.history()[static_cast<std::size_t>(value)]);
        return Repaint;
    case Action::Character:
        return type_character(static_cast<char>(value));
    case Action::EraseCharacter:
        return erase_character();
    case Action::ClearAddress:
        address_input_.clear();
        return Repaint;
    case Action::SubmitAddress: {
        Page page;
        if (resolve_address(address_input_, page))
            return navigate(page);
        notice_ = "Indirizzo non disponibile: siti esterni bloccati";
        return Repaint;
    }
    case Action::ClosePanel:
        panel_ = Panel::None;
        set_focus(Action::Address);
        return Repaint;
    case Action::HistoryPrevious:
        history_offset_ = history_offset_ >= 8 ? history_offset_ - 8 : 0;
        set_focus(Action::HistoryEntry, static_cast<int>(history_offset_));
        return Repaint;
    case Action::HistoryNext:
        history_offset_ += 8;
        set_focus(Action::HistoryEntry, static_cast<int>(history_offset_));
        return Repaint;
    case Action::ClearHistory:
        state_.clear_history();
        history_offset_ = 0;
        set_focus(Action::AskClearData);
        notice_ = "Cronologia cancellata";
        return Repaint | Persist;
    case Action::AskClearData:
        panel_ = Panel::ConfirmClear;
        set_focus(Action::ClosePanel);
        return Repaint;
    case Action::ConfirmClearData:
        state_.clear_data();
        panel_ = Panel::None;
        set_focus(Action::Address);
        notice_ = "Dati locali cancellati";
        return Repaint | LoadPage | Persist;
    }
    return 0;
}

} // namespace glassglide
