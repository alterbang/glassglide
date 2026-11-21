#pragma once

#include <memory>
#include <string>

struct wl_display;
struct wl_registry;
struct wl_shell;
struct wl_shell_surface;
struct wl_surface;
struct wl_webos_shell;
struct wl_webos_shell_surface;

class WebosShell {
public:
    struct Callbacks {
        void (*resize)(void* data, int width, int height);
        void (*state)(void* data, unsigned int state);
        void (*close)(void* data);
        void (*failure)(void* data, const char* message);
    };

    static std::unique_ptr<WebosShell> create(wl_display*, wl_surface*,
        const char* app_id, Callbacks, void* data, std::string& error);
    ~WebosShell();

    WebosShell(const WebosShell&) = delete;
    WebosShell& operator=(const WebosShell&) = delete;

    wl_shell* shell() const { return shell_; }
    wl_webos_shell* webos_shell() const { return webos_shell_; }

private:
    WebosShell(Callbacks callbacks, void* data);

    Callbacks callbacks_;
    void* data_;
    wl_registry* registry_ = nullptr;
    wl_shell* shell_ = nullptr;
    wl_shell_surface* shell_surface_ = nullptr;
    wl_webos_shell* webos_shell_ = nullptr;
    wl_webos_shell_surface* webos_surface_ = nullptr;
    unsigned int shell_name_ = 0;
    unsigned int webos_shell_name_ = 0;
    bool xdg_shell_present_ = false;
    bool initialized_ = false;
};
