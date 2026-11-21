#include "local_pages.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace glassglide;

int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--demo-html")) {
        std::puts(local_page(Page::Demo).c_str());
        return 0;
    }
    assert(argc == 1);
    Page result = Page::Home;
    assert(decide_navigation("about:blank", false, result) == NavigationDecision::AllowLocal);
    assert(decide_navigation("about:blank", true, result) == NavigationDecision::AllowLocal);
    assert(decide_navigation("glass://demo/", true, result) == NavigationDecision::OpenPage);
    assert(result == Page::Demo);
    assert(decide_navigation("glass://demo/", false, result) == NavigationDecision::Block);
    for (const char* uri : { "https://example.com", "http://example.com", "file:///etc/passwd",
             "data:text/html,test", "javascript:alert(1)", "glass://demo/extra",
             "glass://demo.example", "glass://demo/?q=1", "about:blank#other" }) {
        assert(decide_navigation(uri, true, result) == NavigationDecision::Block);
        assert(decide_navigation(uri, false, result) == NavigationDecision::Block);
    }
    assert(decide_navigation(nullptr, true, result) == NavigationDecision::Block);

    for (Page page : std::array { Page::Home, Page::Discover, Page::Demo, Page::Guide }) {
        std::string html = local_page(page);
        assert(html.find("Content-Security-Policy") != std::string::npos);
        assert(html.find("default-src 'none'") != std::string::npos);
        assert(html.find("form-action 'none'") != std::string::npos);
        std::size_t position = 0;
        unsigned links = 0;
        while ((position = html.find("href=\"", position)) != std::string::npos) {
            position += 6;
            auto end = html.find('"', position);
            assert(end != std::string::npos);
            std::string uri = html.substr(position, end - position);
            assert(decide_navigation(uri.c_str(), true, result) == NavigationDecision::OpenPage);
            ++links;
            position = end + 1;
        }
        assert(links > 0);
    }
    std::puts("Local pages and navigation policy: OK");
    return 0;
}
