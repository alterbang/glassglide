#pragma once

#include "browser_ui.h"

#include <memory>
#include <string>

struct _WPEView;
struct _WPEToplevel;
struct wl_compositor;
struct wl_display;
struct wl_shell;
struct wl_shm;
struct wl_surface;
struct wl_webos_shell;

namespace glassglide {

class NativeChrome {
public:
    struct Callbacks {
        void (*effect)(void* data, unsigned effects);
        void (*failure)(void* data, const char* message);
    };

    static std::unique_ptr<NativeChrome> create(wl_display*, wl_compositor*, wl_shm*,
        wl_shell*, wl_webos_shell*, wl_surface* parent, _WPEToplevel*, _WPEView*,
        BrowserUI&, Callbacks, void* data, int width, int height, std::string& error);
    ~NativeChrome();

    NativeChrome(const NativeChrome&) = delete;
    NativeChrome& operator=(const NativeChrome&) = delete;

    void resize(int width, int height);
    void redraw();

private:
    class Impl;
    explicit NativeChrome(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};

} // namespace glassglide
