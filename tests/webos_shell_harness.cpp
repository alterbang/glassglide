#include "../src/webos_shell.h"

#include <wayland-client.h>

#include <cstring>
#include <string>

namespace {

struct State {
    int width = 0;
    int height = 0;
    unsigned int shell_state = 0;
    bool closed = false;
    bool failed = false;
};

void resize(void* data, int width, int height)
{
    auto* state = static_cast<State*>(data);
    state->width = width;
    state->height = height;
}

void close(void* data)
{
    static_cast<State*>(data)->closed = true;
}

void surface_state(void* data, unsigned int state)
{
    static_cast<State*>(data)->shell_state = state;
}

void failure(void* data, const char*)
{
    static_cast<State*>(data)->failed = true;
}

void global(void* data, wl_registry* registry, uint32_t name,
    const char* interface, uint32_t)
{
    if (!std::strcmp(interface, "wl_compositor"))
        *static_cast<wl_compositor**>(data) = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 1));
}

void global_remove(void*, wl_registry*, uint32_t) { }

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    const std::string mode(argv[1]);
    wl_display* display = wl_display_connect(nullptr);
    if (!display)
        return 3;
    wl_registry* registry = wl_display_get_registry(display);
    wl_compositor* compositor = nullptr;
    const wl_registry_listener listener = {global, global_remove};
    wl_registry_add_listener(registry, &listener, &compositor);
    if (wl_display_roundtrip(display) < 0 || !compositor)
        return 4;
    wl_surface* surface = wl_compositor_create_surface(compositor);
    State state;
    std::string error;
    auto shell = WebosShell::create(display, surface, "org.glassglide.browser",
        {resize, surface_state, close, failure}, &state, error);

    bool ok;
    if (mode == "missing" || mode == "disappeared")
        ok = !shell && error.find("both wl_shell and wl_webos_shell") != std::string::npos;
    else if (mode == "xdg")
        ok = !shell && error.find("not xdg_wm_base") != std::string::npos;
    else {
        ok = shell && wl_display_roundtrip(display) >= 0
            && wl_display_roundtrip(display) >= 0;
        if (mode == "ready")
            ok = ok && state.width == 1920 && state.height == 1080
                && state.shell_state == 3 && state.closed && !state.failed;
        else
            ok = ok && state.failed;
    }

    shell.reset();
    wl_surface_destroy(surface);
    wl_compositor_destroy(compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return ok ? 0 : 1;
}
