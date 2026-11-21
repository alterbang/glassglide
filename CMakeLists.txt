cmake_minimum_required(VERSION 3.16)
project(GlassGlide VERSION 0.1.0 LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WPE REQUIRED IMPORTED_TARGET
    wpe-webkit-2.0
    wpe-platform-wayland-2.0
    wayland-client)
pkg_check_modules(CHROME_FONTS REQUIRED IMPORTED_TARGET fontconfig freetype2)
find_library(WEBOS_WAYLAND_CLIENT NAMES wayland-webos-client REQUIRED)

add_executable(glassglide
    src/browser_state.cpp
    src/browser_store.cpp
    src/browser_ui.cpp
    src/local_pages.cpp
    src/main.cpp
    src/native_chrome.cpp
    src/webos_shell.cpp)
target_compile_features(glassglide PRIVATE cxx_std_17)
target_compile_options(glassglide PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(glassglide PRIVATE PkgConfig::WPE PkgConfig::CHROME_FONTS ${WEBOS_WAYLAND_CLIENT})

add_executable(glassglide-launch src/launcher.c)
target_compile_features(glassglide-launch PRIVATE c_std_11)
target_compile_options(glassglide-launch PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(glassglide-launch PRIVATE -static)

install(TARGETS glassglide RUNTIME DESTINATION bin)
install(TARGETS glassglide-launch RUNTIME DESTINATION .)
