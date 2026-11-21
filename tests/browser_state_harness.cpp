#include "browser_state.h"
#include "browser_store.h"

#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace glassglide;

int main(int argc, char** argv)
{
    assert(argc == 2);
    Page result = Page::Home;
    assert(resolve_address(" GLASS://DEMO ", result) && result == Page::Demo);
    assert(resolve_address("glass://demo/", result) && result == Page::Demo);
    assert(resolve_address("guida", result) && result == Page::Guide);
    assert(!resolve_address("https://example.com", result));
    assert(!resolve_address("glass://demo/extra", result));

    BrowserState state;
    state.navigate(Page::Discover);
    state.navigate(Page::Demo);
    assert(state.can_back() && state.back());
    assert(state.current_page() == Page::Discover && state.can_forward());
    state.navigate(Page::Guide);
    assert(!state.can_forward());
    assert(state.toggle_favorite() && state.is_favorite(Page::Guide));
    assert(state.new_tab(false));
    state.navigate(Page::Demo);
    const auto normal_history = state.history();
    assert(state.new_tab(true));
    state.navigate(Page::Discover);
    assert(state.history() == normal_history);
    assert(!state.toggle_favorite());

    const std::string serialized = state.serialize();
    BrowserState restored;
    std::string error;
    assert(restored.restore(serialized, error));
    assert(restored.tabs().size() == 2);
    assert(restored.tabs()[0].trail.size() == 3);
    assert(restored.tabs()[1].trail.back() == Page::Demo);
    assert(restored.active_tab() == 0);
    assert(restored.favorites() == state.favorites());
    assert(restored.history() == normal_history);
    assert(!restored.restore("GLASSGLIDE 1\nA 0\nF 00\nH \nT 0 0\n", error));
    assert(!error.empty() && restored.tabs().size() == 2);
    assert(!restored.restore("GLASSGLIDE 1\nA 0\nF \nH \nT 1 0\n", error));
    assert(!restored.restore("GLASSGLIDE 1\nA 0\nF \nH 4\nT 0 0\n", error));
    assert(!restored.restore("GLASSGLIDE 2\n", error));

    restored.clear_history();
    assert(restored.history().empty() && restored.is_favorite(Page::Guide));
    for (const auto& tab : restored.tabs())
        assert(tab.trail.size() == 1 && tab.position == 0);
    restored.clear_data();
    assert(restored.tabs().size() == 1 && restored.favorites().empty());
    assert(!restored.close_tab(0));
    for (std::size_t i = 1; i < BrowserState::max_tabs; ++i)
        assert(restored.new_tab(false));
    assert(!restored.new_tab(false));
    assert(restored.close_tab(1));
    assert(restored.active_tab() == BrowserState::max_tabs - 2);
    assert(restored.select_tab(0));

    std::string directory = std::string(argv[1]) + "/data";
    BrowserStore store;
    assert(store.open(directory, error));
    std::string loaded;
    assert(store.load(loaded, error) == BrowserStore::LoadResult::Missing);
    assert(store.save(serialized, error));
    assert(store.load(loaded, error) == BrowserStore::LoadResult::Loaded);
    assert(loaded == serialized);
    struct stat status;
    assert(!stat((directory + "/state").c_str(), &status));
    assert((status.st_mode & 0777) == 0600);

    assert(!unlink((directory + "/state").c_str()));
    assert(!symlink("/etc/passwd", (directory + "/state").c_str()));
    assert(store.load(loaded, error) == BrowserStore::LoadResult::Error);
    assert(!error.empty());
    assert(store.save(serialized, error));
    assert(store.load(loaded, error) == BrowserStore::LoadResult::Loaded);
    assert(loaded == serialized);

    std::string link = std::string(argv[1]) + "/link";
    assert(!symlink(directory.c_str(), link.c_str()));
    BrowserStore unsafe_link;
    assert(!unsafe_link.open(link, error));
    assert(!chmod(directory.c_str(), 0755));
    BrowserStore unsafe_permissions;
    assert(!unsafe_permissions.open(directory, error));
    assert(!chmod(directory.c_str(), 0700));

    std::puts("Browser state, privacy and durable storage: OK");
    return 0;
}
