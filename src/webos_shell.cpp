#include "webos_shell.h"

#include <wayland-client.h>
#include <wayland-webos-shell-client-protocol.h>

#include <cerrno>
#include <cstring>

WebosShell::WebosShell(Callbacks callbacks, void* data)
    : callbacks_(callbacks)
    , data_(data)
{
}

std::unique_ptr<WebosShell> WebosShell::create(wl_display* display,
    wl_surface* surface, const char* app_id, Callbacks callbacks, void* data,
    std::string& error)
{
    if (!display || !surface || !app_id || !*app_id || !callbacks.resize
        || !callbacks.state || !callbacks.close || !callbacks.failure) {
        error = "Invalid Wayland shell initialization arguments";
        return nullptr;
    }

    auto result = std::unique_ptr<WebosShell>(new WebosShell(callbacks, data));
    result->registry_ = wl_display_get_registry(display);
    if (!result->registry_) {
        error = "Cannot obtain the Wayland registry";
        return nullptr;
    }

    static const wl_registry_listener registry_listener = {
        [](void* data, wl_registry* registry, uint32_t name,
            const char* interface, uint32_t version) {
            auto* self = static_cast<WebosShell*>(data);
            if (!std::strcmp(interface, "xdg_wm_base")) {
                self->xdg_shell_present_ = true;
            } else if (version >= 1 && !std::strcmp(interface, "wl_shell")
                && !self->shell_) {
                self->shell_ = static_cast<wl_shell*>(
                    wl_registry_bind(registry, name, &wl_shell_interface, 1));
                self->shell_name_ = name;
            } else if (version >= 1 && !std::strcmp(interface, "wl_webos_shell")
                && !self->webos_shell_) {
                self->webos_shell_ = static_cast<wl_webos_shell*>(
                    wl_registry_bind(registry, name, &wl_webos_shell_interface, 1));
                self->webos_shell_name_ = name;
            }
        },
        [](void* data, wl_registry*, uint32_t name) {
            auto* self = static_cast<WebosShell*>(data);
            bool shell_removed = self->shell_ && name == self->shell_name_;
            bool webos_removed = self->webos_shell_ && name == self->webos_shell_name_;
            if (!shell_removed && !webos_removed)
                return;
            if (self->initialized_) {
                self->callbacks_.failure(self->data_, "A required Wayland shell disappeared");
            } else if (shell_removed) {
                wl_shell_destroy(self->shell_);
                self->shell_ = nullptr;
                self->shell_name_ = 0;
            } else {
                wl_webos_shell_destroy(self->webos_shell_);
                self->webos_shell_ = nullptr;
                self->webos_shell_name_ = 0;
            }
        }
    };
    if (wl_registry_add_listener(result->registry_, &registry_listener, result.get()) < 0
        || wl_display_roundtrip(display) < 0) {
        error = "Cannot enumerate Wayland shell globals";
        return nullptr;
    }
    if (result->xdg_shell_present_) {
        error = "This prototype requires the legacy webOS shell, not xdg_wm_base";
        return nullptr;
    }
    if (!result->shell_ || !result->webos_shell_) {
        error = "The compositor did not expose both wl_shell and wl_webos_shell";
        return nullptr;
    }

    static const wl_shell_surface_listener shell_listener = {
        [](void*, wl_shell_surface* shell_surface, uint32_t serial) {
            wl_shell_surface_pong(shell_surface, serial);
        },
        [](void* data, wl_shell_surface*, uint32_t, int32_t width, int32_t height) {
            auto* self = static_cast<WebosShell*>(data);
            if (width > 0 && height > 0)
                self->callbacks_.resize(self->data_, width, height);
        },
        [](void*, wl_shell_surface*) { }
    };
    result->shell_surface_ = wl_shell_get_shell_surface(result->shell_, surface);
    if (!result->shell_surface_
        || wl_shell_surface_add_listener(result->shell_surface_, &shell_listener, result.get()) < 0) {
        error = "Cannot create the wl_shell surface";
        return nullptr;
    }

    static const wl_webos_shell_surface_listener webos_listener = {
        [](void* data, wl_webos_shell_surface*, uint32_t state) {
            auto* self = static_cast<WebosShell*>(data);
            self->callbacks_.state(self->data_, state);
        },
        [](void*, wl_webos_shell_surface*, int32_t, int32_t) { },
        [](void* data, wl_webos_shell_surface*) {
            auto* self = static_cast<WebosShell*>(data);
            self->callbacks_.close(self->data_);
        },
        [](void*, wl_webos_shell_surface*, wl_array*) { },
        [](void*, wl_webos_shell_surface*, uint32_t) { },
        [](void*, wl_webos_shell_surface*, uint32_t) { }
    };
    result->webos_surface_ = wl_webos_shell_get_shell_surface(result->webos_shell_, surface);
    if (!result->webos_surface_
        || wl_webos_shell_surface_add_listener(result->webos_surface_, &webos_listener, result.get()) < 0) {
        error = "Cannot create the webOS shell surface";
        return nullptr;
    }

    wl_webos_shell_surface_set_property(result->webos_surface_, "appId", app_id);
    wl_webos_shell_surface_set_key_mask(result->webos_surface_,
        WL_WEBOS_SHELL_SURFACE_WEBOS_KEY_DEFAULT
            | WL_WEBOS_SHELL_SURFACE_WEBOS_KEY_BACK
            | WL_WEBOS_SHELL_SURFACE_WEBOS_KEY_EXIT);
    wl_webos_shell_surface_set_state(result->webos_surface_, WL_WEBOS_SHELL_SURFACE_STATE_FULLSCREEN);
    wl_shell_surface_set_title(result->shell_surface_, "GlassGlide");
    wl_shell_surface_set_fullscreen(result->shell_surface_,
        WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT, 0, nullptr);
    if (wl_display_flush(display) < 0 && errno != EAGAIN) {
        error = "Cannot send Wayland shell requests";
        return nullptr;
    }
    result->initialized_ = true;
    return result;
}

WebosShell::~WebosShell()
{
    if (webos_surface_)
        wl_webos_shell_surface_destroy(webos_surface_);
    if (shell_surface_)
        wl_shell_surface_destroy(shell_surface_);
    if (webos_shell_)
        wl_webos_shell_destroy(webos_shell_);
    if (shell_)
        wl_shell_destroy(shell_);
    if (registry_)
        wl_registry_destroy(registry_);
}
