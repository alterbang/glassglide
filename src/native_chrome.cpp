#include "native_chrome.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <wayland-client.h>
#include <wayland-webos-shell-client-protocol.h>
#include <wayland-webos-surface-group-client-protocol.h>
#include <wpe/wpe-platform.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <set>
#include <sys/mman.h>
#include <unistd.h>

namespace glassglide {

namespace {

constexpr unsigned int webos_key_back = 0x01200003;
constexpr unsigned int webos_key_exit = 0x01200001;

struct Color {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

std::string system_error(const char* operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

class Canvas {
public:
    Canvas(std::uint32_t* pixels, int width, int height, double scale, int origin, FT_Face font)
        : pixels_(pixels), width_(width), height_(height), scale_(scale), origin_(origin), font_(font)
    {
    }

    void rectangle(int x, int y, int width, int height, Color color)
    {
        int left = std::clamp(origin_ + static_cast<int>(std::floor(x * scale_)), 0, width_);
        int top = std::clamp(static_cast<int>(std::floor(y * scale_)), 0, height_);
        int right = std::clamp(origin_ + static_cast<int>(std::ceil((x + width) * scale_)), 0, width_);
        int bottom = std::clamp(static_cast<int>(std::ceil((y + height) * scale_)), 0, height_);
        std::uint32_t pixel = 0xff000000u | (static_cast<std::uint32_t>(color.red) << 16)
            | (static_cast<std::uint32_t>(color.green) << 8) | color.blue;
        for (int row = top; row < bottom; ++row)
            std::fill(pixels_ + row * width_ + left, pixels_ + row * width_ + right, pixel);
    }

    bool text(int x, int baseline, int size, const std::string& value, Color color,
        int max_width, bool centered = false)
    {
        unsigned pixel_size = std::max(1, static_cast<int>(std::round(size * scale_)));
        if (FT_Set_Pixel_Sizes(font_, 0, pixel_size))
            return false;
        int pixel_limit = static_cast<int>(max_width * scale_);
        int pen = origin_ + static_cast<int>(x * scale_);
        if (centered) {
            int measured = 0;
            for (const char* cursor = value.c_str(); *cursor; cursor = g_utf8_next_char(cursor)) {
                gunichar code = g_utf8_get_char_validated(cursor, -1);
                if (code == static_cast<gunichar>(-1) || code == static_cast<gunichar>(-2))
                    return false;
                if (FT_Load_Char(font_, code, FT_LOAD_DEFAULT))
                    return false;
                measured += font_->glyph->advance.x >> 6;
            }
            pen += std::max(0, (pixel_limit - measured) / 2);
        }
        int end = origin_ + static_cast<int>(x * scale_) + pixel_limit;
        int line = static_cast<int>(baseline * scale_);
        for (const char* cursor = value.c_str(); *cursor; cursor = g_utf8_next_char(cursor)) {
            gunichar code = g_utf8_get_char_validated(cursor, -1);
            if (code == static_cast<gunichar>(-1) || code == static_cast<gunichar>(-2))
                return false;
            if (FT_Load_Char(font_, code, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL))
                return false;
            auto* glyph = font_->glyph;
            if (pen + (glyph->advance.x >> 6) > end)
                break;
            const FT_Bitmap& bitmap = glyph->bitmap;
            if (bitmap.width && bitmap.rows && bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
                return false;
            for (unsigned row = 0; row < bitmap.rows; ++row) {
                int py = line - glyph->bitmap_top + static_cast<int>(row);
                if (py < 0 || py >= height_)
                    continue;
                const unsigned char* source = bitmap.pitch >= 0
                    ? bitmap.buffer + row * bitmap.pitch
                    : bitmap.buffer + (bitmap.rows - 1 - row) * -bitmap.pitch;
                for (unsigned column = 0; column < bitmap.width; ++column) {
                    int px = pen + glyph->bitmap_left + static_cast<int>(column);
                    if (px < 0 || px >= width_ || px >= end)
                        continue;
                    unsigned alpha = source[column];
                    unsigned inverse = 255 - alpha;
                    std::uint32_t& destination = pixels_[py * width_ + px];
                    unsigned old_alpha = destination >> 24;
                    unsigned old_red = (destination >> 16) & 255;
                    unsigned old_green = (destination >> 8) & 255;
                    unsigned old_blue = destination & 255;
                    unsigned a = alpha + (old_alpha * inverse + 127) / 255;
                    unsigned r = (color.red * alpha + old_red * inverse + 127) / 255;
                    unsigned g = (color.green * alpha + old_green * inverse + 127) / 255;
                    unsigned b = (color.blue * alpha + old_blue * inverse + 127) / 255;
                    destination = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
            pen += glyph->advance.x >> 6;
        }
        return true;
    }

private:
    std::uint32_t* pixels_;
    int width_;
    int height_;
    double scale_;
    int origin_;
    FT_Face font_;
};

} // namespace

class NativeChrome::Impl {
public:
    Impl(BrowserUI& ui, Callbacks callbacks, void* data)
        : ui_(ui), callbacks_(callbacks), data_(data)
    {
        for (auto& buffer : buffers_)
            buffer.owner = this;
    }

    ~Impl()
    {
        shutting_down_ = true;
        if (event_handler_)
            g_signal_handler_disconnect(view_, event_handler_);
        if (surface_) {
            wl_surface_attach(surface_, nullptr, 0, 0);
            wl_surface_commit(surface_);
        }
        if (webos_surface_)
            wl_webos_shell_surface_destroy(webos_surface_);
        if (shell_surface_)
            wl_shell_surface_destroy(shell_surface_);
        if (surface_)
            wl_surface_destroy(surface_);
        if (layer_)
            wl_webos_surface_group_layer_destroy(layer_);
        if (group_)
            wl_webos_surface_group_destroy(group_);
        if (group_compositor_)
            wl_webos_surface_group_compositor_destroy(group_compositor_);
        if (registry_)
            wl_registry_destroy(registry_);
        for (auto& buffer : buffers_)
            destroy_buffer(buffer);
        if (font_)
            FT_Done_Face(font_);
        if (freetype_)
            FT_Done_FreeType(freetype_);
    }

    bool initialize(wl_display* display, wl_compositor* compositor, wl_shm* shm,
        wl_shell* shell, wl_webos_shell* webos_shell, wl_surface* parent,
        WPEToplevel* toplevel, WPEView* view, int width, int height, std::string& error)
    {
        if (!display || !compositor || !shm || !shell || !webos_shell || !parent
            || !toplevel || !view || !callbacks_.effect || !callbacks_.failure) {
            error = "Argomenti non validi per i controlli nativi";
            return false;
        }
        display_ = display;
        compositor_ = compositor;
        shm_ = shm;
        view_ = view;
        if (!initialize_font(error))
            return false;

        static const wl_registry_listener registry_listener {
            [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
                auto* self = static_cast<Impl*>(data);
                if (!std::strcmp(interface, "wl_webos_surface_group_compositor")
                    && version >= 1 && !self->group_compositor_) {
                    self->group_compositor_ = static_cast<wl_webos_surface_group_compositor*>(
                        wl_registry_bind(registry, name, &wl_webos_surface_group_compositor_interface, 1));
                    self->group_name_ = name;
                }
            },
            [](void* data, wl_registry*, uint32_t name) {
                auto* self = static_cast<Impl*>(data);
                if (name == self->group_name_)
                    self->fail("Il compositor dei controlli webOS non e piu disponibile");
            },
        };
        registry_ = wl_display_get_registry(display_);
        if (!registry_ || wl_registry_add_listener(registry_, &registry_listener, this) < 0
            || wl_display_roundtrip(display_) < 0 || !group_compositor_) {
            error = "Il compositor non offre i gruppi di superfici webOS";
            return false;
        }

        surface_ = wl_compositor_create_surface(compositor_);
        if (!surface_) {
            error = "Impossibile creare la superficie dei controlli";
            return false;
        }
        wl_surface_set_user_data(surface_, toplevel);
        shell_surface_ = wl_shell_get_shell_surface(shell, surface_);
        static const wl_shell_surface_listener shell_listener {
            [](void*, wl_shell_surface* surface, uint32_t serial) {
                wl_shell_surface_pong(surface, serial);
            },
            [](void*, wl_shell_surface*, uint32_t, int32_t, int32_t) { },
            [](void*, wl_shell_surface*) { },
        };
        if (!shell_surface_ || wl_shell_surface_add_listener(shell_surface_, &shell_listener, this) < 0) {
            error = "Impossibile creare la finestra dei controlli";
            return false;
        }
        wl_shell_surface_set_transient(shell_surface_, parent, 0, 0, WL_SHELL_SURFACE_TRANSIENT_INACTIVE);
        webos_surface_ = wl_webos_shell_get_shell_surface(webos_shell, surface_);
        static const wl_webos_shell_surface_listener webos_listener {
            [](void*, wl_webos_shell_surface*, uint32_t) { },
            [](void*, wl_webos_shell_surface*, int32_t, int32_t) { },
            [](void* data, wl_webos_shell_surface*) {
                static_cast<Impl*>(data)->fail("La superficie dei controlli e stata chiusa");
            },
            [](void*, wl_webos_shell_surface*, wl_array*) { },
            [](void*, wl_webos_shell_surface*, uint32_t) { },
            [](void*, wl_webos_shell_surface*, uint32_t) { },
        };
        if (!webos_surface_
            || wl_webos_shell_surface_add_listener(webos_surface_, &webos_listener, this) < 0) {
            error = "Impossibile registrare la superficie dei controlli webOS";
            return false;
        }
        wl_webos_shell_surface_set_property(webos_surface_, "appId", "org.glassglide.browser");
        wl_webos_shell_surface_set_key_mask(webos_surface_,
            WL_WEBOS_SHELL_SURFACE_WEBOS_KEY_DEFAULT
                | WL_WEBOS_SHELL_SURFACE_WEBOS_KEY_BACK
                | WL_WEBOS_SHELL_SURFACE_WEBOS_KEY_EXIT);
        group_ = wl_webos_surface_group_compositor_create_surface_group(
            group_compositor_, parent, "org.glassglide.browser");
        static const wl_webos_surface_group_listener group_listener {
            [](void* data, wl_webos_surface_group*) {
                static_cast<Impl*>(data)->fail("Il gruppo delle superfici webOS e stato chiuso");
            },
        };
        if (!group_ || wl_webos_surface_group_add_listener(group_, &group_listener, this) < 0) {
            error = "Impossibile creare il gruppo di superfici webOS";
            return false;
        }
        layer_ = wl_webos_surface_group_create_layer(group_, "controls", 1);
        static const wl_webos_surface_group_layer_listener layer_listener {
            [](void* data, wl_webos_surface_group_layer*) {
                static_cast<Impl*>(data)->attached_ = true;
            },
            [](void* data, wl_webos_surface_group_layer*) {
                auto* self = static_cast<Impl*>(data);
                self->attached_ = false;
                if (self->initialized_)
                    self->fail("I controlli non sono piu collegati alla finestra");
            },
        };
        if (!layer_ || wl_webos_surface_group_layer_add_listener(layer_, &layer_listener, this) < 0) {
            error = "Impossibile creare il livello dei controlli";
            return false;
        }
        wl_webos_surface_group_attach(group_, surface_, "controls");
        if (!set_size(width, height, error) || !paint(error))
            return false;
        if (wl_display_roundtrip(display_) < 0 || !attached_) {
            error = "Il compositor non ha collegato il livello dei controlli";
            return false;
        }
        initialized_ = true;
        event_handler_ = g_signal_connect(view_, "event", G_CALLBACK(on_event), this);
        if (!event_handler_) {
            error = "Impossibile ascoltare l'input dei controlli";
            return false;
        }
        return true;
    }

    void resize(int width, int height)
    {
        std::string error;
        if (!set_size(width, height, error)) {
            fail(error.c_str());
            return;
        }
        redraw();
    }

    void redraw()
    {
        if (shutting_down_ || failed_)
            return;
        dirty_ = true;
        std::string error;
        if (!paint(error))
            fail(error.c_str());
    }

private:
    struct Buffer {
        Impl* owner = nullptr;
        wl_buffer* object = nullptr;
        void* memory = MAP_FAILED;
        std::size_t size = 0;
        int width = 0;
        int height = 0;
        bool busy = false;
    };

    bool initialize_font(std::string& error)
    {
        if (FT_Init_FreeType(&freetype_) || !FcInit()) {
            error = "Impossibile inizializzare i font dei controlli";
            return false;
        }
        FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>("sans-serif"));
        if (!pattern) {
            error = "Impossibile cercare il font dei controlli";
            return false;
        }
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result;
        FcPattern* match = FcFontMatch(nullptr, pattern, &result);
        FcPatternDestroy(pattern);
        FcChar8* filename = nullptr;
        if (!match || FcPatternGetString(match, FC_FILE, 0, &filename) != FcResultMatch
            || FT_New_Face(freetype_, reinterpret_cast<const char*>(filename), 0, &font_)) {
            if (match)
                FcPatternDestroy(match);
            error = "Nessun font utilizzabile per i controlli";
            return false;
        }
        FcPatternDestroy(match);
        return true;
    }

    bool set_size(int width, int height, std::string& error)
    {
        if (width < 640 || height < 360 || width > 4096 || height > 2160) {
            error = "Dimensioni dello schermo non supportate dai controlli";
            return false;
        }
        width_ = width;
        height_ = height;
        scale_ = std::min(width / 1280.0, height / 720.0);
        origin_ = static_cast<int>((width - 1280 * scale_) / 2);
        dirty_ = true;
        return true;
    }

    void fail(const char* message)
    {
        if (shutting_down_ || failed_)
            return;
        failed_ = true;
        callbacks_.failure(data_, message);
    }

    void destroy_buffer(Buffer& buffer)
    {
        if (buffer.object)
            wl_buffer_destroy(buffer.object);
        if (buffer.memory != MAP_FAILED)
            munmap(buffer.memory, buffer.size);
        buffer.object = nullptr;
        buffer.memory = MAP_FAILED;
        buffer.size = 0;
        buffer.width = 0;
        buffer.height = 0;
        buffer.busy = false;
    }

    bool prepare_buffer(Buffer& buffer, int width, int height, std::string& error)
    {
        if (buffer.object && buffer.width == width && buffer.height == height)
            return true;
        destroy_buffer(buffer);
        std::size_t size = static_cast<std::size_t>(width) * height * 4;
        if (size > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
            error = "Buffer dei controlli troppo grande";
            return false;
        }
        char path[] = "/tmp/glassglide-ui-XXXXXX";
        int fd = mkstemp(path);
        if (fd < 0) {
            error = system_error("Impossibile creare il buffer dei controlli");
            return false;
        }
        if (unlink(path) < 0 || ftruncate(fd, static_cast<off_t>(size)) < 0) {
            error = system_error("Impossibile preparare il buffer dei controlli");
            close(fd);
            return false;
        }
        buffer.memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (buffer.memory == MAP_FAILED) {
            error = system_error("Impossibile mappare il buffer dei controlli");
            close(fd);
            return false;
        }
        buffer.size = size;
        buffer.width = width;
        buffer.height = height;
        wl_shm_pool* pool = wl_shm_create_pool(shm_, fd, static_cast<int32_t>(size));
        close(fd);
        if (!pool) {
            error = "Impossibile creare il pool dei controlli";
            return false;
        }
        buffer.object = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4,
            WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        static const wl_buffer_listener listener {
            [](void* data, wl_buffer*) {
                auto* buffer = static_cast<Buffer*>(data);
                buffer->busy = false;
                if (buffer->owner->dirty_)
                    buffer->owner->redraw();
            },
        };
        if (!buffer.object || wl_buffer_add_listener(buffer.object, &listener, &buffer) < 0) {
            error = "Impossibile creare il buffer Wayland dei controlli";
            return false;
        }
        return true;
    }

    bool paint(std::string& error)
    {
        if (!dirty_)
            return true;
        auto found = std::find_if(buffers_.begin(), buffers_.end(),
            [](const Buffer& buffer) { return !buffer.busy; });
        if (found == buffers_.end())
            return true;
        int target_height = std::min(height_, static_cast<int>(std::ceil(ui_.overlay_height() * scale_)));
        if (!prepare_buffer(*found, width_, target_height, error))
            return false;
        std::memset(found->memory, 0, found->size);
        Canvas canvas(static_cast<std::uint32_t*>(found->memory), width_, target_height,
            scale_, origin_, font_);
        constexpr Color white { 236, 245, 255 };
        constexpr Color muted { 171, 194, 215 };
        constexpr Color mint { 132, 229, 207 };
        canvas.rectangle(-origin_ / scale_, 0, width_ / scale_ + 2, 140, { 11, 24, 40 });
        canvas.rectangle(-origin_ / scale_, 137, width_ / scale_ + 2, 3, { 35, 67, 87 });
        if (!canvas.text(24, 35, 20, "GlassGlide", mint, 150)
            || !canvas.text(24, 124, 15, ui_.notice(), muted, 1000)
            || !canvas.text(1060, 124, 14,
                ui_.page_focused() ? "PAGINA ATTIVA" : "ANTEPRIMA LOCALE", mint, 192, true)) {
            error = "Impossibile disegnare il testo dei controlli";
            return false;
        }

        if (ui_.panel() != Panel::None) {
            canvas.rectangle(165, 150, 850, 480, { 18, 38, 57 });
            canvas.rectangle(166, 151, 848, 478, { 22, 46, 67 });
            const char* heading = ui_.panel() == Panel::Address ? "Indirizzo o pagina locale"
                : ui_.panel() == Panel::Favorites ? "Preferiti"
                : ui_.panel() == Panel::History ? "Cronologia"
                : "Cancellare tutti i dati locali?";
            const char* description = ui_.panel() == Panel::Address
                ? "Solo le pagine locali sono disponibili in questa anteprima."
                : ui_.panel() == Panel::Favorites ? "Le pagine salvate per ritrovarle facilmente."
                : ui_.panel() == Panel::History ? "Le schede private non compaiono qui."
                : "Saranno rimossi preferiti, cronologia e schede salvate.";
            if (!canvas.text(186, 178, 23, heading, white, 790)
                || !canvas.text(186, 201, 15, description, muted, 790)) {
                error = "Impossibile disegnare il pannello dei controlli";
                return false;
            }
            if (ui_.panel() == Panel::Address) {
                canvas.rectangle(186, 207, 800, 34, { 9, 24, 39 });
                if (!canvas.text(197, 230, 17, ui_.address_input(), white, 778)) {
                    error = "Impossibile disegnare l'indirizzo";
                    return false;
                }
            }
        }

        for (const auto& button : ui_.buttons()) {
            Color fill = button.enabled ? Color { 40, 62, 81 } : Color { 26, 42, 57 };
            Color text = button.enabled ? white : Color { 106, 126, 142 };
            if (button.active)
                fill = { 37, 101, 100 };
            if (button.focused) {
                canvas.rectangle(button.rect.x - 3, button.rect.y - 3,
                    button.rect.width + 6, button.rect.height + 6, mint);
                fill = { 36, 83, 91 };
            }
            if (button.action == Action::Address)
                fill = button.focused ? Color { 36, 83, 91 } : Color { 8, 21, 33 };
            canvas.rectangle(button.rect.x, button.rect.y, button.rect.width, button.rect.height, fill);
            bool centered = button.action != Action::Address;
            int inset = centered ? 4 : 12;
            if (!canvas.text(button.rect.x + inset,
                    button.rect.y + button.rect.height / 2 + 6,
                    button.action == Action::Character ? 18 : 15,
                    button.label, text, button.rect.width - 2 * inset, centered)) {
                error = "Impossibile disegnare un controllo";
                return false;
            }
        }

        wl_region* region = wl_compositor_create_region(compositor_);
        if (!region) {
            error = "Impossibile creare la regione di input dei controlli";
            return false;
        }
        wl_region_add(region, 0, 0, width_, target_height);
        wl_surface_set_input_region(surface_, region);
        wl_region_destroy(region);
        wl_surface_attach(surface_, found->object, 0, 0);
        wl_surface_damage(surface_, 0, 0, width_, target_height);
        wl_surface_commit(surface_);
        found->busy = true;
        dirty_ = false;
        if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
            error = system_error("Impossibile aggiornare i controlli");
            return false;
        }
        return true;
    }

    static gboolean on_event(WPEView*, WPEEvent* event, gpointer data)
    {
        auto* self = static_cast<Impl*>(data);
        WPEEventType type = wpe_event_get_event_type(event);
        if (type == WPE_EVENT_KEYBOARD_KEY_DOWN || type == WPE_EVENT_KEYBOARD_KEY_UP) {
            unsigned keycode = wpe_event_keyboard_get_keycode(event);
            if (type == WPE_EVENT_KEYBOARD_KEY_UP)
                return self->suppressed_keys_.erase(keycode) != 0;
            unsigned key = wpe_event_keyboard_get_keyval(event);
            unsigned effects = 0;
            bool handled = true;
            if (key == WPE_KEY_Back || key == WPE_KEY_Escape || key == webos_key_back)
                effects = self->ui_.back_or_exit();
            else if (key == webos_key_exit)
                effects = Exit;
            else if (self->ui_.page_focused())
                handled = false;
            else if (key == WPE_KEY_Left)
                effects = self->ui_.move(Direction::Left);
            else if (key == WPE_KEY_Right)
                effects = self->ui_.move(Direction::Right);
            else if (key == WPE_KEY_Up)
                effects = self->ui_.move(Direction::Up);
            else if (key == WPE_KEY_Down)
                effects = self->ui_.move(Direction::Down);
            else if (key == WPE_KEY_Return || key == WPE_KEY_KP_Enter)
                effects = self->ui_.activate_focused();
            else if (key == WPE_KEY_BackSpace && self->ui_.panel() == Panel::Address)
                effects = self->ui_.erase_character();
            else {
                auto modifiers = wpe_event_get_modifiers(event);
                unsigned character = wpe_keyval_to_unicode(key);
                if (self->ui_.panel() == Panel::Address && character >= 32 && character <= 126
                    && !(modifiers & (WPE_MODIFIER_KEYBOARD_CONTROL
                        | WPE_MODIFIER_KEYBOARD_ALT | WPE_MODIFIER_KEYBOARD_META)))
                    effects = self->ui_.type_character(static_cast<char>(character));
                else
                    handled = false;
            }
            if (handled)
                self->suppressed_keys_.insert(keycode);
            if (effects)
                self->callbacks_.effect(self->data_, effects);
            return handled;
        }
        if (type == WPE_EVENT_POINTER_DOWN || type == WPE_EVENT_POINTER_UP
            || type == WPE_EVENT_POINTER_MOVE || type == WPE_EVENT_TOUCH_DOWN
            || type == WPE_EVENT_TOUCH_UP || type == WPE_EVENT_TOUCH_MOVE) {
            double x, y;
            if (!wpe_event_get_position(event, &x, &y)
                || y < 0 || y >= self->ui_.overlay_height() * self->scale_)
                return FALSE;
            if (type == WPE_EVENT_TOUCH_DOWN || (type == WPE_EVENT_POINTER_DOWN
                && wpe_event_pointer_button_get_button(event) == WPE_BUTTON_PRIMARY)) {
                unsigned effects = self->ui_.pointer_down((x - self->origin_) / self->scale_, y / self->scale_);
                if (effects)
                    self->callbacks_.effect(self->data_, effects);
            }
            return TRUE;
        }
        return FALSE;
    }

    BrowserUI& ui_;
    Callbacks callbacks_;
    void* data_;
    wl_display* display_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    WPEView* view_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_webos_surface_group_compositor* group_compositor_ = nullptr;
    wl_webos_surface_group* group_ = nullptr;
    wl_webos_surface_group_layer* layer_ = nullptr;
    wl_surface* surface_ = nullptr;
    wl_shell_surface* shell_surface_ = nullptr;
    wl_webos_shell_surface* webos_surface_ = nullptr;
    FT_Library freetype_ = nullptr;
    FT_Face font_ = nullptr;
    std::array<Buffer, 2> buffers_;
    std::set<unsigned> suppressed_keys_;
    gulong event_handler_ = 0;
    uint32_t group_name_ = 0;
    int width_ = 0;
    int height_ = 0;
    double scale_ = 1;
    int origin_ = 0;
    bool attached_ = false;
    bool initialized_ = false;
    bool dirty_ = false;
    bool shutting_down_ = false;
    bool failed_ = false;
};

NativeChrome::NativeChrome(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

NativeChrome::~NativeChrome() = default;

std::unique_ptr<NativeChrome> NativeChrome::create(wl_display* display, wl_compositor* compositor,
    wl_shm* shm, wl_shell* shell, wl_webos_shell* webos_shell, wl_surface* parent,
    WPEToplevel* toplevel, WPEView* view, BrowserUI& ui, Callbacks callbacks, void* data,
    int width, int height, std::string& error)
{
    auto impl = std::make_unique<Impl>(ui, callbacks, data);
    if (!impl->initialize(display, compositor, shm, shell, webos_shell, parent,
            toplevel, view, width, height, error))
        return nullptr;
    return std::unique_ptr<NativeChrome>(new NativeChrome(std::move(impl)));
}

void NativeChrome::resize(int width, int height)
{
    impl_->resize(width, height);
}

void NativeChrome::redraw()
{
    impl_->redraw();
}

} // namespace glassglide
