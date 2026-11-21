#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace glassglide {

enum class Page : unsigned char { Home, Discover, Demo, Guide };

struct PageInfo {
    Page id;
    const char* address;
    const char* title;
};

const std::array<PageInfo, 4>& pages();
const PageInfo& page_info(Page page);
bool resolve_address(const std::string& input, Page& page);

struct Tab {
    bool private_mode = false;
    std::vector<Page> trail { Page::Home };
    std::size_t position = 0;
};

class BrowserState {
public:
    static constexpr std::size_t max_tabs = 6;
    static constexpr std::size_t max_trail = 32;
    static constexpr std::size_t max_history = 48;

    BrowserState();

    const std::vector<Tab>& tabs() const { return tabs_; }
    const Tab& current_tab() const { return tabs_[active_]; }
    std::size_t active_tab() const { return active_; }
    Page current_page() const { return current_tab().trail[current_tab().position]; }
    const std::vector<Page>& favorites() const { return favorites_; }
    const std::vector<Page>& history() const { return history_; }

    bool new_tab(bool private_mode);
    bool close_tab(std::size_t index);
    bool select_tab(std::size_t index);
    void navigate(Page page);
    bool back();
    bool forward();
    bool can_back() const;
    bool can_forward() const;
    bool is_favorite(Page page) const;
    bool toggle_favorite();
    void clear_history();
    void clear_data();

    std::string serialize() const;
    bool restore(const std::string& data, std::string& error);

private:
    void record(Page page);

    std::vector<Tab> tabs_;
    std::size_t active_ = 0;
    std::vector<Page> favorites_;
    std::vector<Page> history_;
};

} // namespace glassglide
