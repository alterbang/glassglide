#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int make_path(char* result, size_t size, const char* root, const char* suffix)
{
    int written = snprintf(result, size, "%s/%s", root, suffix);
    if (written < 0 || (size_t)written >= size) {
        fprintf(stderr, "GlassGlide: application path is too long\n");
        return -1;
    }
    return 0;
}

int main(void)
{
    char root[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", root, sizeof(root) - 1);
    if (length <= 0 || (size_t)length >= sizeof(root) - 1) {
        fprintf(stderr, "GlassGlide: cannot determine application directory\n");
        return EXIT_FAILURE;
    }
    root[length] = '\0';
    char* slash = strrchr(root, '/');
    if (!slash) {
        fprintf(stderr, "GlassGlide: invalid application path\n");
        return EXIT_FAILURE;
    }
    *slash = '\0';

    char binary[PATH_MAX];
    char library[PATH_MAX];
    char helpers[PATH_MAX];
    char bundle[PATH_MAX];
    char gio_modules[PATH_MAX];
    char process[PATH_MAX];
    if (make_path(binary, sizeof(binary), root, "bin/glassglide")
        || make_path(library, sizeof(library), root, "lib")
        || make_path(helpers, sizeof(helpers), root, "libexec/wpe-webkit-2.0")
        || make_path(bundle, sizeof(bundle), root, "lib/wpe-webkit-2.0/injected-bundle")
        || make_path(gio_modules, sizeof(gio_modules), root, "lib/gio/modules"))
        return EXIT_FAILURE;

    if (access(binary, X_OK)
        || make_path(process, sizeof(process), helpers, "WPEWebProcess")
        || access(process, X_OK)
        || make_path(process, sizeof(process), helpers, "WPENetworkProcess")
        || access(process, X_OK)
        || make_path(process, sizeof(process), library, "libWPEWebKit-2.0.so.1")
        || access(process, R_OK)
        || make_path(process, sizeof(process), bundle, "libWPEInjectedBundle.so")
        || access(process, R_OK)) {
        fprintf(stderr, "GlassGlide: incomplete private WPE runtime\n");
        return EXIT_FAILURE;
    }

    const char* clear[] = {"LD_PRELOAD", "LD_AUDIT", "LD_BIND_NOW",
        "WEBKIT_INSPECTOR_HTTP_SERVER", "WEBKIT_INSPECTOR_SERVER",
        "WEBKIT_INSPECTOR_RESOURCES_PATH", "WPE_PLATFORMS_PATH"};
    for (size_t i = 0; i < sizeof(clear) / sizeof(clear[0]); ++i) {
        if (unsetenv(clear[i]) < 0) {
            perror("GlassGlide: cannot sanitize environment");
            return EXIT_FAILURE;
        }
    }
    /* Skia composition renders white on the target TV; use TextureMapper. */
    if (setenv("LD_LIBRARY_PATH", library, 1)
        || setenv("WEBKIT_EXEC_PATH", helpers, 1)
        || setenv("WEBKIT_INJECTED_BUNDLE_PATH", bundle, 1)
        || setenv("WEBKIT_USE_SKIA_FOR_COMPOSITION", "0", 1)
        || setenv("GIO_MODULE_DIR", gio_modules, 1)
        || ((!getenv("XDG_RUNTIME_DIR") || !*getenv("XDG_RUNTIME_DIR"))
            && setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 1))
        || ((!getenv("WAYLAND_DISPLAY") || !*getenv("WAYLAND_DISPLAY"))
            && setenv("WAYLAND_DISPLAY", "wayland-0", 1))) {
        perror("GlassGlide: cannot configure environment");
        return EXIT_FAILURE;
    }

    char* const arguments[] = {binary, NULL};
    execv(binary, arguments);
    perror("GlassGlide: cannot start application");
    return EXIT_FAILURE;
}
