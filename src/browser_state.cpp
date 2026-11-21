#include "browser_state.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace glassglide {

const std::array<PageInfo, 4>& pages()
{
    static const std::array<PageInfo, 4> result {{
        { Page::Home, "glass://home", "Home" },
        { Page::Discover, "glass://scopri", "Scopri" },
        { Page::Demo, "glass://demo", "Pagina di prova" },
        { Page::Guide, "glass://guida", "Guida" },
    }};
    return result;
}

const PageInfo& page_info(Page page)
{
    return pages().at(static_cast<std::size_t>(page));
}

bool resolve_address(const std::string& input, Page& page)
{
    auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return false;
    auto last = input.find_last_not_of(" \t\r\n");
    std::string value = input.substr(first, last - first + 1);
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    for (const auto& entry : pages()) {
        if (value == entry.address || value == std::string(entry.address) + "/"
            || value == entry.address + 8) {
            page = entry.id;
            return true;
        }
    }
    return false;
}

BrowserState::BrowserState()
    : tabs_(1)
{
}

void BrowserState::record(Page page)
{
    if (current_tab().private_mode)
        return;
    history_.insert(history_.begin(), page);
    if (history_.size() > max_history)
        history_.pop_back();
}

bool BrowserState::new_tab(bool private_mode)
{
    if (tabs_.size() == max_tabs)
        return false;
    tabs_.push_back(Tab { private_mode, { Page::Home }, 0 });
    active_ = tabs_.size() - 1;
    record(Page::Home);
    return true;
}

bool BrowserState::close_tab(std::size_t index)
{
    if (index >= tabs_.size() || tabs_.size() == 1)
        return false;
    tabs_.erase(tabs_.begin() + index);
    if (active_ > index)
        --active_;
    else if (active_ >= tabs_.size())
        active_ = tabs_.size() - 1;
    return true;
}

bool BrowserState::select_tab(std::size_t index)
{
    if (index >= tabs_.size() || index == active_)
        return false;
    active_ = index;
    return true;
}

void BrowserState::navigate(Page page)
{
    auto& tab = tabs_[active_];
    if (tab.trail[tab.position] == page)
        return;
    tab.trail.erase(tab.trail.begin() + tab.position + 1, tab.trail.end());
    tab.trail.push_back(page);
    if (tab.trail.size() > max_trail)
        tab.trail.erase(tab.trail.begin());
    tab.position = tab.trail.size() - 1;
    record(page);
}

bool BrowserState::can_back() const
{
    return current_tab().position > 0;
}

bool BrowserState::can_forward() const
{
    return current_tab().position + 1 < current_tab().trail.size();
}

bool BrowserState::back()
{
    if (!can_back())
        return false;
    --tabs_[active_].position;
    record(current_page());
    return true;
}

bool BrowserState::forward()
{
    if (!can_forward())
        return false;
    ++tabs_[active_].position;
    record(current_page());
    return true;
}

bool BrowserState::is_favorite(Page page) const
{
    return std::find(favorites_.begin(), favorites_.end(), page) != favorites_.end();
}

bool BrowserState::toggle_favorite()
{
    if (current_tab().private_mode)
        return false;
    Page page = current_page();
    auto found = std::find(favorites_.begin(), favorites_.end(), page);
    if (found == favorites_.end())
        favorites_.push_back(page);
    else
        favorites_.erase(found);
    return true;
}

void BrowserState::clear_history()
{
    history_.clear();
    for (auto& tab : tabs_) {
        Page current = tab.trail[tab.position];
        tab.trail.assign(1, current);
        tab.position = 0;
    }
}

void BrowserState::clear_data()
{
    tabs_.assign(1, Tab { });
    active_ = 0;
    favorites_.clear();
    history_.clear();
}

static char page_code(Page page)
{
    return static_cast<char>('0' + static_cast<unsigned>(page));
}

static bool decode_pages(const std::string& text, std::size_t maximum, std::vector<Page>& result)
{
    if (text.size() > maximum)
        return false;
    for (char code : text) {
        if (code < '0' || code >= '0' + static_cast<int>(pages().size()))
            return false;
        result.push_back(static_cast<Page>(code - '0'));
    }
    return true;
}

std::string BrowserState::serialize() const
{
    std::vector<const Tab*> normal_tabs;
    std::size_t active = 0;
    for (std::size_t index = 0; index < tabs_.size(); ++index) {
        if (tabs_[index].private_mode)
            continue;
        if (index == active_)
            active = normal_tabs.size();
        normal_tabs.push_back(&tabs_[index]);
    }

    std::ostringstream stream;
    stream << "GLASSGLIDE 1\nA " << active << "\nF ";
    for (Page page : favorites_)
        stream << page_code(page);
    stream << "\nH ";
    for (Page page : history_)
        stream << page_code(page);
    stream << '\n';
    if (normal_tabs.empty())
        stream << "T 0 0\n";
    for (const Tab* tab : normal_tabs) {
        stream << "T " << tab->position << ' ';
        for (Page page : tab->trail)
            stream << page_code(page);
        stream << '\n';
    }
    return stream.str();
}

bool BrowserState::restore(const std::string& data, std::string& error)
{
    std::istringstream input(data);
    std::string line;
    if (!std::getline(input, line) || line != "GLASSGLIDE 1") {
        error = "Formato dei dati non riconosciuto";
        return false;
    }
    std::vector<Tab> restored_tabs;
    std::vector<Page> favorites;
    std::vector<Page> history;
    std::size_t active = 0;
    bool have_active = false;
    bool have_favorites = false;
    bool have_history = false;
    while (std::getline(input, line)) {
        if (line.size() < 2 || line[1] != ' ') {
            error = "Dati di navigazione non validi";
            return false;
        }
        std::string value = line.substr(2);
        if (line[0] == 'A' && !have_active) {
            if (value.size() != 1 || value[0] < '0' || value[0] >= '0' + max_tabs) {
                error = "Scheda attiva non valida";
                return false;
            }
            active = value[0] - '0';
            have_active = true;
        } else if (line[0] == 'F' && !have_favorites) {
            if (!decode_pages(value, pages().size(), favorites)) {
                error = "Preferiti non validi";
                return false;
            }
            auto unique = favorites;
            std::sort(unique.begin(), unique.end());
            if (std::adjacent_find(unique.begin(), unique.end()) != unique.end()) {
                error = "Preferiti duplicati";
                return false;
            }
            have_favorites = true;
        } else if (line[0] == 'H' && !have_history) {
            if (!decode_pages(value, max_history, history)) {
                error = "Cronologia non valida";
                return false;
            }
            have_history = true;
        } else if (line[0] == 'T') {
            auto space = value.find(' ');
            if (space == std::string::npos || !space || space > 2
                || restored_tabs.size() == max_tabs
                || !std::all_of(value.begin(), value.begin() + space,
                    [](unsigned char ch) { return std::isdigit(ch); })) {
                error = "Scheda non valida";
                return false;
            }
            std::size_t position = 0;
            for (std::size_t i = 0; i < space; ++i)
                position = position * 10 + value[i] - '0';
            std::vector<Page> trail;
            if (!decode_pages(value.substr(space + 1), max_trail, trail)
                || trail.empty() || position >= trail.size()) {
                error = "Navigazione della scheda non valida";
                return false;
            }
            restored_tabs.push_back(Tab { false, std::move(trail), position });
        } else {
            error = "Dati di navigazione non validi";
            return false;
        }
    }
    if (!have_active || !have_favorites || !have_history
        || restored_tabs.empty() || active >= restored_tabs.size()) {
        error = "Dati di navigazione incompleti";
        return false;
    }
    tabs_ = std::move(restored_tabs);
    active_ = active;
    favorites_ = std::move(favorites);
    history_ = std::move(history);
    error.clear();
    return true;
}

} // namespace glassglide
