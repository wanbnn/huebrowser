cmake_minimum_required(VERSION 3.16)
project(HueBrowser VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(WEBKIT REQUIRED IMPORTED_TARGET webkit2gtk-4.1)

add_executable(hue-browser src/main.c)
target_link_libraries(hue-browser PRIVATE PkgConfig::GTK PkgConfig::WEBKIT)
target_compile_options(hue-browser PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS hue-browser RUNTIME DESTINATION bin)
