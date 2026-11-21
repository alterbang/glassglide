#define _GNU_SOURCE

#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wpe/wayland/wpe-wayland.h>
#include <xkbcommon/xkbcommon.h>

struct Server;

struct Seat {
    struct Server *server;
    struct wl_global *global;
    struct wl_resource *seat;
    struct wl_resource *keyboard;
    struct wl_resource *pointer;
    struct wl_resource *touch;
    unsigned capabilities;
};

struct Server {
    struct wl_display *display;
    struct wl_resource *surface;
    unsigned surfaces;
    struct Seat seats[4];
    FILE *keymap;
    unsigned keymap_size;
    unsigned phase;
    int failed;
};

static void server_fail(struct Server *server, const char *message)
{
    fprintf(stderr, "Fake compositor: %s\n", message);
    server->failed = 1;
    wl_display_terminate(server->display);
}

static struct wl_resource *new_resource(struct wl_client *client,
    const struct wl_interface *interface, uint32_t version, uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(client, interface, version, id);
    if (!resource) {
        fprintf(stderr, "Fake compositor: cannot allocate resource\n");
        abort();
    }
    return resource;
}

static void destroy_resource(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client, struct wl_resource *resource,
    struct wl_resource *buffer, int32_t x, int32_t y)
{
    (void)client; (void)resource; (void)buffer; (void)x; (void)y;
}

static void surface_damage(struct wl_client *client, struct wl_resource *resource,
    int32_t x, int32_t y, int32_t width, int32_t height)
{
    (void)client; (void)resource; (void)x; (void)y; (void)width; (void)height;
}

static void surface_frame(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    (void)resource;
    struct wl_resource *callback = new_resource(client, &wl_callback_interface, 1, id);
    wl_callback_send_done(callback, 0);
    wl_resource_destroy(callback);
}

static void surface_region(struct wl_client *client, struct wl_resource *resource,
    struct wl_resource *region)
{
    (void)client; (void)resource; (void)region;
}

static void surface_commit(struct wl_client *client, struct wl_resource *resource)
{
    (void)client; (void)resource;
}

static const struct wl_surface_interface surface_interface = {
    .destroy = destroy_resource,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = surface_frame,
    .set_opaque_region = surface_region,
    .set_input_region = surface_region,
    .commit = surface_commit,
};

static void create_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct Server *server = wl_resource_get_user_data(resource);
    struct wl_resource *surface = new_resource(client, &wl_surface_interface, 1, id);
    wl_resource_set_implementation(surface, &surface_interface, NULL, NULL);
    if (++server->surfaces == 2)
        server->surface = surface;
}

static void region_change(struct wl_client *client, struct wl_resource *resource,
    int32_t x, int32_t y, int32_t width, int32_t height)
{
    (void)client; (void)resource; (void)x; (void)y; (void)width; (void)height;
}

static const struct wl_region_interface region_interface = {
    .destroy = destroy_resource,
    .add = region_change,
    .subtract = region_change,
};

static void create_region(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    (void)resource;
    struct wl_resource *region = new_resource(client, &wl_region_interface, 1, id);
    wl_resource_set_implementation(region, &region_interface, NULL, NULL);
}

static const struct wl_compositor_interface compositor_interface = {
    .create_surface = create_surface,
    .create_region = create_region,
};

static void bind_compositor(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource = new_resource(client, &wl_compositor_interface, version, id);
    wl_resource_set_implementation(resource, &compositor_interface, data, NULL);
}

static void bind_output(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *resource = new_resource(client, &wl_output_interface, version, id);
    wl_output_send_geometry(resource, 0, 0, 160, 90, WL_OUTPUT_SUBPIXEL_UNKNOWN,
        "GlassGlide", "Test", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, 640, 360, 60000);
}

static void send_enter_key(struct Seat *seat, uint32_t key)
{
    struct wl_array keys;
    wl_array_init(&keys);
    wl_keyboard_send_enter(seat->keyboard, 1, seat->server->surface, &keys);
    wl_keyboard_send_key(seat->keyboard, 2, 1, key, WL_KEYBOARD_KEY_STATE_PRESSED);
    wl_keyboard_send_key(seat->keyboard, 3, 2, key, WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_array_release(&keys);
}

static void set_cursor(struct wl_client *client, struct wl_resource *resource,
    uint32_t serial, struct wl_resource *surface, int32_t x, int32_t y)
{
    (void)client; (void)resource; (void)serial; (void)surface; (void)x; (void)y;
}

static const struct wl_pointer_interface pointer_interface = {
    .set_cursor = set_cursor,
};

static void get_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct Seat *seat = wl_resource_get_user_data(resource);
    seat->pointer = new_resource(client, &wl_pointer_interface, 1, id);
    wl_resource_set_implementation(seat->pointer, &pointer_interface, NULL, NULL);
}

static void get_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct Seat *seat = wl_resource_get_user_data(resource);
    seat->keyboard = new_resource(client, &wl_keyboard_interface, 1, id);
    wl_keyboard_send_keymap(seat->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
        fileno(seat->server->keymap), seat->server->keymap_size);
    if (seat == &seat->server->seats[3] && seat->server->phase == 4)
        send_enter_key(seat, 28);
}

static void get_touch(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct Seat *seat = wl_resource_get_user_data(resource);
    seat->touch = new_resource(client, &wl_touch_interface, 1, id);
}

static const struct wl_seat_interface seat_interface = {
    .get_pointer = get_pointer,
    .get_keyboard = get_keyboard,
    .get_touch = get_touch,
};

static void bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct Seat *seat = data;
    seat->seat = new_resource(client, &wl_seat_interface, version, id);
    wl_resource_set_implementation(seat->seat, &seat_interface, seat, NULL);
    wl_seat_send_capabilities(seat->seat, seat->capabilities);
}

static void add_seat(struct Server *server, unsigned index, unsigned capabilities)
{
    struct Seat *seat = &server->seats[index];
    seat->server = server;
    seat->capabilities = capabilities;
    seat->global = wl_global_create(server->display, &wl_seat_interface, 1, seat, bind_seat);
    if (!seat->global)
        server_fail(server, "cannot create seat");
}

static int handle_command(int fd, uint32_t mask, void *data)
{
    struct Server *server = data;
    char command;
    (void)mask;
    ssize_t count = read(fd, &command, 1);
    if (count != 1) {
        server_fail(server, "client ended before completing the test");
        return 0;
    }

    switch (command) {
    case 'R':
        if (!server->surface || !server->seats[0].keyboard || !server->seats[1].keyboard
            || !server->seats[0].pointer || !server->seats[1].pointer
            || server->seats[0].touch || server->seats[1].touch
            || !server->seats[2].pointer || !server->seats[2].touch) {
            server_fail(server, "unexpected initial seat bindings");
            break;
        }
        wl_pointer_send_enter(server->seats[0].pointer, 10, server->surface,
            wl_fixed_from_int(10), wl_fixed_from_int(20));
        wl_pointer_send_button(server->seats[0].pointer, 11, 1, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
        wl_pointer_send_button(server->seats[0].pointer, 12, 2, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
        break;
    case 'P':
        wl_pointer_send_enter(server->seats[2].pointer, 13, server->surface,
            wl_fixed_from_int(30), wl_fixed_from_int(40));
        wl_pointer_send_leave(server->seats[0].pointer, 14, server->surface);
        wl_pointer_send_motion(server->seats[2].pointer, 3,
            wl_fixed_from_int(50), wl_fixed_from_int(60));
        wl_pointer_send_button(server->seats[2].pointer, 15, 4, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
        wl_pointer_send_button(server->seats[2].pointer, 16, 5, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
        break;
    case 'Q':
        wl_seat_send_capabilities(server->seats[2].seat, WL_SEAT_CAPABILITY_TOUCH);
        wl_pointer_send_enter(server->seats[0].pointer, 17, server->surface,
            wl_fixed_from_int(70), wl_fixed_from_int(80));
        wl_pointer_send_button(server->seats[0].pointer, 18, 6, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
        wl_pointer_send_button(server->seats[0].pointer, 19, 7, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
        break;
    case 'S':
        wl_seat_send_capabilities(server->seats[2].seat,
            WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH);
        send_enter_key(&server->seats[0], 103);
        break;
    case '1':
        send_enter_key(&server->seats[1], 108);
        break;
    case '2':
        wl_keyboard_send_leave(server->seats[0].keyboard, 4, server->surface);
        wl_keyboard_send_key(server->seats[1].keyboard, 5, 3, 106, WL_KEYBOARD_KEY_STATE_PRESSED);
        wl_keyboard_send_key(server->seats[1].keyboard, 6, 4, 106, WL_KEYBOARD_KEY_STATE_RELEASED);
        break;
    case '3':
        wl_global_destroy(server->seats[1].global);
        server->seats[1].global = NULL;
        break;
    case '4':
        server->phase = 4;
        add_seat(server, 3, WL_SEAT_CAPABILITY_KEYBOARD);
        break;
    case '5':
        wl_seat_send_capabilities(server->seats[3].seat, 0);
        break;
    case '6':
        wl_seat_send_capabilities(server->seats[0].seat, 0);
        break;
    case '7':
        wl_global_destroy(server->seats[2].global);
        server->seats[2].global = NULL;
        break;
    case 'X':
        wl_display_terminate(server->display);
        break;
    default:
        server_fail(server, "unexpected test command");
    }
    wl_display_flush_clients(server->display);
    return 0;
}

struct Client {
    GMainLoop *loop;
    WPEDisplay *display;
    WPEView *view;
    int fd;
    unsigned phase;
    unsigned focus_in;
    unsigned focus_out;
    guint timeout;
    int failed;
};

static void client_fail(struct Client *client, const char *message)
{
    fprintf(stderr, "WPE client: %s (phase %u)\n", message, client->phase);
    client->failed = 1;
    g_main_loop_quit(client->loop);
}

static void command(struct Client *client, char value)
{
    if (write(client->fd, &value, 1) != 1)
        client_fail(client, "cannot send test command");
}

static gboolean on_event(WPEView *view, WPEEvent *event, gpointer data)
{
    struct Client *client = data;
    if (wpe_event_get_event_type(event) == WPE_EVENT_POINTER_DOWN) {
        double x, y;
        const double expected[][2] = { { 10, 20 }, { 50, 60 }, { 70, 80 } };
        if (client->phase < 10 || client->phase > 12
            || !wpe_event_get_position(event, &x, &y)
            || x != expected[client->phase - 10][0] || y != expected[client->phase - 10][1]
            || wpe_event_pointer_button_get_button(event) != WPE_BUTTON_PRIMARY) {
            client_fail(client, "wrong pointer, coordinates or shared focus");
            return TRUE;
        }
        const char commands[] = { 'P', 'Q', 'S' };
        command(client, commands[client->phase - 10]);
        client->phase = client->phase == 12 ? 0 : client->phase + 1;
        return TRUE;
    }
    if (wpe_event_get_event_type(event) != WPE_EVENT_KEYBOARD_KEY_DOWN)
        return FALSE;

    const unsigned keys[] = { WPE_KEY_Up, WPE_KEY_Down, WPE_KEY_Right, WPE_KEY_Return };
    const unsigned phases[] = { 0, 1, 2, 4 };
    const char commands[] = { '1', '2', '3', '5' };
    unsigned index;
    for (index = 0; index < 4 && phases[index] != client->phase; ++index) { }
    if (index == 4 || wpe_event_keyboard_get_keyval(event) != keys[index]
        || !wpe_view_get_has_focus(view)
        || client->focus_in != (index == 3 ? 2u : 1u)
        || client->focus_out != (index == 3 ? 1u : 0u)) {
        client_fail(client, "wrong key or keyboard focus");
        return TRUE;
    }
    client->phase = index == 3 ? 5 : index + 1;
    command(client, commands[index]);
    return TRUE;
}

static void on_focus(WPEView *view, GParamSpec *property, gpointer data)
{
    struct Client *client = data;
    (void)property;
    if (wpe_view_get_has_focus(view)) {
        ++client->focus_in;
        return;
    }

    ++client->focus_out;
    if (client->phase == 3) {
        client->phase = 4;
        command(client, '4');
    } else if (client->phase == 5) {
        client->phase = 6;
        command(client, '6');
    } else
        client_fail(client, "unexpected focus loss");
}

static void on_devices(WPEDisplay *display, GParamSpec *property, gpointer data)
{
    struct Client *client = data;
    (void)property;
    unsigned devices = wpe_display_get_available_input_devices(display);
    if (client->phase == 6 && !(devices & WPE_AVAILABLE_INPUT_DEVICE_KEYBOARD)) {
        if (devices != (WPE_AVAILABLE_INPUT_DEVICE_MOUSE | WPE_AVAILABLE_INPUT_DEVICE_TOUCHSCREEN)) {
            client_fail(client, "remaining input devices were not preserved");
            return;
        }
        client->phase = 7;
        command(client, '7');
    }
}

static void on_disconnected(WPEDisplay *display, GError *error, gpointer data)
{
    struct Client *client = data;
    (void)display;
    if (client->phase != 7 || !error
        || !strstr(error->message, "Primary Wayland input seat disappeared")) {
        client_fail(client, "unexpected display disconnection");
        return;
    }
    client->phase = 8;
    g_main_loop_quit(client->loop);
}

static gboolean on_timeout(gpointer data)
{
    struct Client *client = data;
    client->timeout = 0;
    client_fail(client, "timed out waiting for input");
    return G_SOURCE_REMOVE;
}

static int run_client(int socket_fd, int command_fd)
{
    char socket_env[32];
    snprintf(socket_env, sizeof(socket_env), "%d", socket_fd);
    if (setenv("WAYLAND_SOCKET", socket_env, 1) < 0) {
        perror("setenv");
        return 1;
    }

    struct Client client = { .fd = command_fd, .phase = 10 };
    client.loop = g_main_loop_new(NULL, FALSE);
    client.display = wpe_display_wayland_new();
    GError *error = NULL;
    if (!wpe_display_wayland_connect(WPE_DISPLAY_WAYLAND(client.display), NULL, &error)) {
        fprintf(stderr, "WPE client: connection failed: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        return 1;
    }

    WPEToplevel *toplevel = wpe_display_create_toplevel(client.display, 1);
    client.view = wpe_view_new(client.display);
    wpe_view_set_toplevel(client.view, toplevel);
    if (!wpe_view_get_mapped(client.view)
        || wpe_display_get_available_input_devices(client.display)
            != (WPE_AVAILABLE_INPUT_DEVICE_MOUSE | WPE_AVAILABLE_INPUT_DEVICE_KEYBOARD
                | WPE_AVAILABLE_INPUT_DEVICE_TOUCHSCREEN)) {
        fprintf(stderr, "WPE client: initial mapping or aggregated devices are incorrect\n");
        return 1;
    }

    g_signal_connect(client.view, "event", G_CALLBACK(on_event), &client);
    g_signal_connect(client.view, "notify::has-focus", G_CALLBACK(on_focus), &client);
    g_signal_connect(client.display, "notify::available-input-devices", G_CALLBACK(on_devices), &client);
    g_signal_connect(client.display, "disconnected", G_CALLBACK(on_disconnected), &client);
    client.timeout = g_timeout_add_seconds(12, on_timeout, &client);
    command(&client, 'R');
    if (!client.failed)
        g_main_loop_run(client.loop);
    if (client.phase == 8)
        command(&client, 'X');
    else
        client.failed = 1;
    if (client.timeout)
        g_source_remove(client.timeout);
    g_signal_handlers_disconnect_by_data(client.view, &client);
    g_signal_handlers_disconnect_by_data(client.display, &client);
    g_object_unref(client.view);
    g_object_unref(toplevel);
    g_object_unref(client.display);
    g_main_loop_unref(client.loop);
    return client.failed;
}

static int create_keymap(struct Server *server)
{
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context)
        return -1;
    const struct xkb_rule_names names = { "evdev", "pc105", "us", "", "" };
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    char *text = keymap ? xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1) : NULL;
    server->keymap = tmpfile();
    if (!text || !server->keymap) {
        free(text);
        if (keymap)
            xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        return -1;
    }
    server->keymap_size = strlen(text) + 1;
    int result = fwrite(text, 1, server->keymap_size, server->keymap) == server->keymap_size
        && !fflush(server->keymap) ? 0 : -1;
    free(text);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    return result;
}

int main(void)
{
    uid_t real_uid, effective_uid, saved_uid;
    if (getresuid(&real_uid, &effective_uid, &saved_uid) < 0
        || !real_uid || !effective_uid || !saved_uid) {
        fprintf(stderr, "Run this isolated WPE test as a non-root user\n");
        return 1;
    }
    int sockets[2];
    int commands[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) || pipe(commands)) {
        perror("socketpair/pipe");
        return 1;
    }
    alarm(20);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (!pid) {
        close(sockets[0]);
        close(commands[0]);
        int result = run_client(sockets[1], commands[1]);
        close(commands[1]);
        _exit(result);
    }

    close(sockets[1]);
    close(commands[1]);
    struct Server server = { 0 };
    server.display = wl_display_create();
    if (!server.display || create_keymap(&server) || wl_display_init_shm(server.display)
        || !wl_client_create(server.display, sockets[0])
        || !wl_global_create(server.display, &wl_compositor_interface, 1, &server, bind_compositor)
        || !wl_global_create(server.display, &wl_output_interface, 1, &server, bind_output)) {
        fprintf(stderr, "Fake compositor: setup failed\n");
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }
    add_seat(&server, 0, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_TOUCH);
    add_seat(&server, 1, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_TOUCH);
    add_seat(&server, 2, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_TOUCH);
    if (!wl_event_loop_add_fd(wl_display_get_event_loop(server.display), commands[0],
            WL_EVENT_READABLE, handle_command, &server)) {
        server_fail(&server, "cannot monitor client commands");
        kill(pid, SIGTERM);
    } else if (!server.failed)
        wl_display_run(server.display);

    int status;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status))
        server.failed = 1;
    wl_display_destroy_clients(server.display);
    wl_display_destroy(server.display);
    fclose(server.keymap);
    close(commands[0]);
    if (server.failed)
        return 1;
    puts("Multiple keyboards and pointers, shared focus, hotplug and primary-seat loss: OK");
    return 0;
}
