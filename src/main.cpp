#include "browser_state.h"
#include "browser_store.h"
#include "browser_ui.h"
#include "local_pages.h"
#include "native_chrome.h"
#include "webos_shell.h"

#include <wayland-webos-shell-client-protocol.h>
#include <wpe/webkit.h>
#include <wpe/wayland/wpe-wayland.h>
#include <wpe/wpe-platform.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unistd.h>

namespace {

constexpr const char* app_id = "org.glassglide.browser";
constexpr const char* data_directory = "/media/developer/.glassglide-data";

struct App {
    GMainLoop* loop = nullptr;
    WPEView* view = nullptr;
    WPEToplevel* toplevel = nullptr;
    WebKitWebView* web_view = nullptr;
    glassglide::BrowserState browser;
    glassglide::BrowserUI ui { browser };
    glassglide::BrowserStore store;
    std::unique_ptr<glassglide::NativeChrome> chrome;
    glassglide::Page pending_page = glassglide::Page::Home;
    guint pending_navigation = 0;
    unsigned long long page_generation = 0;
    unsigned long long pending_generation = 0;
    unsigned int shell_state = WL_WEBOS_SHELL_SURFACE_STATE_DEFAULT;
    int status = EXIT_SUCCESS;
};

void fail(App* app, const char* message)
{
    std::fprintf(stderr, "GlassGlide: %s\n", message);
    app->status = EXIT_FAILURE;
    g_main_loop_quit(app->loop);
}

void load_page(App* app)
{
    ++app->page_generation;
    std::string html = glassglide::local_page(app->browser.current_page());
    webkit_web_view_load_html(app->web_view, html.c_str(), nullptr);
}

void apply_effect(void* data, unsigned effects)
{
    auto* app = static_cast<App*>(data);
    if (effects & glassglide::Persist) {
        std::string error;
        if (!app->store.save(app->browser.serialize(), error)) {
            std::fprintf(stderr, "GlassGlide: dati non salvati: %s\n", error.c_str());
            app->ui.set_notice("Dati non salvati: " + error);
        }
    }
    if (effects & glassglide::LoadPage)
        load_page(app);
    if (effects & glassglide::Exit)
        g_main_loop_quit(app->loop);
    if ((effects & glassglide::Repaint) && app->chrome)
        app->chrome->redraw();
}

gboolean apply_pending_navigation(gpointer data)
{
    auto* app = static_cast<App*>(data);
    app->pending_navigation = 0;
    if (app->pending_generation == app->page_generation)
        apply_effect(app, app->ui.navigate(app->pending_page));
    return G_SOURCE_REMOVE;
}

void resize_view(void* data, int width, int height)
{
    auto* app = static_cast<App*>(data);
    wpe_toplevel_resized(app->toplevel, width, height);
    wpe_view_resized(app->view, width, height);
    if (app->chrome)
        app->chrome->resize(width, height);
}

void close_view(void* data)
{
    g_main_loop_quit(static_cast<App*>(data)->loop);
}

void update_toplevel_state(App* app)
{
    unsigned int state = wpe_view_get_has_focus(app->view) ? WPE_TOPLEVEL_STATE_ACTIVE : 0;
    if (app->shell_state == WL_WEBOS_SHELL_SURFACE_STATE_FULLSCREEN)
        state |= WPE_TOPLEVEL_STATE_FULLSCREEN;
    else if (app->shell_state == WL_WEBOS_SHELL_SURFACE_STATE_MAXIMIZED)
        state |= WPE_TOPLEVEL_STATE_MAXIMIZED;
    wpe_toplevel_state_changed(app->toplevel, static_cast<WPEToplevelState>(state));
}

void shell_state_changed(void* data, unsigned int state)
{
    auto* app = static_cast<App*>(data);
    if (state > WL_WEBOS_SHELL_SURFACE_STATE_FULLSCREEN) {
        fail(app, "Unsupported webOS surface state");
        return;
    }
    app->shell_state = state;
    if (state == WL_WEBOS_SHELL_SURFACE_STATE_MINIMIZED)
        wpe_view_unmap(app->view);
    else
        wpe_view_map(app->view);
    update_toplevel_state(app);
}

void focus_changed(WPEView*, GParamSpec*, gpointer data)
{
    update_toplevel_state(static_cast<App*>(data));
}

void shell_failure(void* data, const char* message)
{
    fail(static_cast<App*>(data), message);
}

void display_failed(WPEDisplay*, GError* error, gpointer data)
{
    std::string message = "Wayland display connection lost: ";
    message += error ? error->message : "unknown error";
    fail(static_cast<App*>(data), message.c_str());
}

gboolean decide_policy(WebKitWebView*, WebKitPolicyDecision* decision,
    WebKitPolicyDecisionType type, gpointer data)
{
    auto* app = static_cast<App*>(data);
    const char* uri = nullptr;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        auto* navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        auto* action = webkit_navigation_policy_decision_get_navigation_action(navigation);
        auto* request = action ? webkit_navigation_action_get_request(action) : nullptr;
        uri = request ? webkit_uri_request_get_uri(request) : nullptr;
    } else if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        auto* response = WEBKIT_RESPONSE_POLICY_DECISION(decision);
        auto* request = webkit_response_policy_decision_get_request(response);
        uri = request ? webkit_uri_request_get_uri(request) : nullptr;
    }

    glassglide::Page page = glassglide::Page::Home;
    auto action = glassglide::decide_navigation(uri,
        type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION, page);
    if (action == glassglide::NavigationDecision::AllowLocal) {
        webkit_policy_decision_use(decision);
    } else {
        webkit_policy_decision_ignore(decision);
        if (action == glassglide::NavigationDecision::OpenPage) {
            app->pending_page = page;
            app->pending_generation = app->page_generation;
            if (!app->pending_navigation)
                app->pending_navigation = g_idle_add(apply_pending_navigation, app);
        } else {
            std::fprintf(stderr, "GlassGlide: external navigation or download blocked\n");
            app->ui.set_notice("Navigazione esterna bloccata");
            if (app->chrome)
                app->chrome->redraw();
        }
    }
    return TRUE;
}

void download_started(WebKitNetworkSession*, WebKitDownload* download, gpointer)
{
    std::fprintf(stderr, "GlassGlide: download blocked\n");
    webkit_download_cancel(download);
}

gboolean load_failed(WebKitWebView*, WebKitLoadEvent, const gchar*, GError* error, gpointer data)
{
    if (error && (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED)
        || g_error_matches(error, WEBKIT_POLICY_ERROR,
            WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE)))
        return TRUE;
    std::string message = "Cannot load the local page: ";
    message += error ? error->message : "unknown error";
    fail(static_cast<App*>(data), message.c_str());
    return TRUE;
}

void web_process_terminated(WebKitWebView*, WebKitWebProcessTerminationReason reason, gpointer data)
{
    std::string message = "Web process terminated (reason ";
    message += std::to_string(static_cast<int>(reason));
    message += ")";
    fail(static_cast<App*>(data), message.c_str());
}

} // namespace

int main()
{
    uid_t real_uid;
    uid_t effective_uid;
    uid_t saved_uid;
    if (getresuid(&real_uid, &effective_uid, &saved_uid) < 0) {
        std::perror("GlassGlide: cannot verify process identity");
        return EXIT_FAILURE;
    }
    if (real_uid == 0 || effective_uid == 0 || saved_uid == 0) {
        std::fprintf(stderr, "GlassGlide: refusing to run the unsandboxed prototype as root\n");
        return EXIT_FAILURE;
    }

    App app;
    app.loop = g_main_loop_new(nullptr, FALSE);
    std::string storage_error;
    if (!app.store.open(data_directory, storage_error))
        app.ui.set_notice("Dati non disponibili: " + storage_error);
    else {
        std::string saved;
        auto loaded = app.store.load(saved, storage_error);
        if (loaded == glassglide::BrowserStore::LoadResult::Loaded
            && !app.browser.restore(saved, storage_error))
            app.ui.set_notice("Dati non leggibili: " + storage_error);
        else if (loaded == glassglide::BrowserStore::LoadResult::Error)
            app.ui.set_notice("Dati non leggibili: " + storage_error);
    }
    if (!storage_error.empty())
        std::fprintf(stderr, "GlassGlide: %s\n", storage_error.c_str());
    WPEDisplay* display = wpe_display_wayland_new();
    GError* error = nullptr;
    if (!display || !wpe_display_wayland_connect(WPE_DISPLAY_WAYLAND(display), nullptr, &error)) {
        std::fprintf(stderr, "GlassGlide: Wayland connection failed: %s\n",
            error ? error->message : "cannot create display");
        g_clear_error(&error);
        if (display)
            g_object_unref(display);
        g_main_loop_unref(app.loop);
        return EXIT_FAILURE;
    }

    auto* wl_display = wpe_display_wayland_get_wl_display(WPE_DISPLAY_WAYLAND(display));
    if (!wpe_display_get_n_screens(display)) {
        std::fprintf(stderr, "GlassGlide: no Wayland output is available\n");
        g_object_unref(display);
        g_main_loop_unref(app.loop);
        return EXIT_FAILURE;
    }
    WPEScreen* screen = wpe_display_get_screen(display, 0);
    int width = wpe_screen_get_width(screen);
    int height = wpe_screen_get_height(screen);
    if (width <= 0 || height <= 0) {
        std::fprintf(stderr, "GlassGlide: Wayland output has no valid dimensions\n");
        g_object_unref(display);
        g_main_loop_unref(app.loop);
        return EXIT_FAILURE;
    }

    WebKitNetworkSession* session = webkit_network_session_new_ephemeral();
    if (!session) {
        std::fprintf(stderr, "GlassGlide: cannot create an ephemeral network session\n");
        g_object_unref(display);
        g_main_loop_unref(app.loop);
        return EXIT_FAILURE;
    }
    g_signal_connect(session, "download-started", G_CALLBACK(download_started), nullptr);
    auto* web_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "display", display, "network-session", session, nullptr));
    g_object_unref(session);
    if (!web_view) {
        std::fprintf(stderr, "GlassGlide: cannot create the WPE WebKit view\n");
        g_object_unref(display);
        g_main_loop_unref(app.loop);
        return EXIT_FAILURE;
    }
    app.web_view = web_view;

    app.view = webkit_web_view_get_wpe_view(web_view);
    if (app.view)
        g_object_ref(app.view);
    app.toplevel = app.view ? wpe_view_get_toplevel(app.view) : nullptr;
    if (!app.toplevel || !WPE_IS_TOPLEVEL_WAYLAND(app.toplevel)) {
        std::fprintf(stderr, "GlassGlide: cannot create a Wayland view\n");
        if (app.view)
            g_object_unref(app.view);
        g_object_unref(web_view);
        g_object_unref(display);
        g_main_loop_unref(app.loop);
        return EXIT_FAILURE;
    }
    resize_view(&app, width, height);

    std::string shell_error;
    auto shell = WebosShell::create(wl_display,
        wpe_toplevel_wayland_get_wl_surface(WPE_TOPLEVEL_WAYLAND(app.toplevel)),
        app_id, {resize_view, shell_state_changed, close_view, shell_failure}, &app, shell_error);
    if (!shell) {
        std::fprintf(stderr, "GlassGlide: %s\n", shell_error.c_str());
        g_object_unref(app.view);
        g_object_unref(web_view);
        g_object_unref(display);
        g_main_loop_unref(app.loop);
        return EXIT_FAILURE;
    }
    gulong focus_handler = g_signal_connect(app.view, "notify::has-focus",
        G_CALLBACK(focus_changed), &app);
    update_toplevel_state(&app);

    WebKitSettings* settings = webkit_web_view_get_settings(web_view);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE);
    webkit_settings_set_allow_file_access_from_file_urls(settings, FALSE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, FALSE);
    g_signal_connect(web_view, "decide-policy", G_CALLBACK(decide_policy), &app);
    g_signal_connect(web_view, "load-failed", G_CALLBACK(load_failed), &app);
    g_signal_connect(web_view, "web-process-terminated", G_CALLBACK(web_process_terminated), &app);
    gulong display_handler = g_signal_connect(display, "disconnected",
        G_CALLBACK(display_failed), &app);

    auto* display_wayland = WPE_DISPLAY_WAYLAND(display);
    std::string chrome_error;
    app.chrome = glassglide::NativeChrome::create(wl_display,
        wpe_display_wayland_get_wl_compositor(display_wayland),
        wpe_display_wayland_get_wl_shm(display_wayland),
        shell->shell(), shell->webos_shell(),
        wpe_toplevel_wayland_get_wl_surface(WPE_TOPLEVEL_WAYLAND(app.toplevel)),
        app.toplevel, app.view, app.ui, { apply_effect, shell_failure },
        &app, width, height, chrome_error);
    if (!app.chrome)
        fail(&app, chrome_error.c_str());
    if (app.status == EXIT_SUCCESS)
        load_page(&app);
    if (app.status == EXIT_SUCCESS)
        g_main_loop_run(app.loop);

    if (app.pending_navigation)
        g_source_remove(app.pending_navigation);
    g_signal_handler_disconnect(display, display_handler);
    g_signal_handler_disconnect(app.view, focus_handler);
    app.chrome.reset();
    g_object_unref(web_view);
    shell.reset();
    g_object_unref(app.view);
    g_object_unref(display);
    g_main_loop_unref(app.loop);
    return app.status;
}
