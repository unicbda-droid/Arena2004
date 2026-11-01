cmake_minimum_required(VERSION 3.16)
project(Arena2004 VERSION 0.9.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(ARENA2004_BUILD_LOBBY "Build the NAT lobby / relay server" ON)
option(ARENA2004_STEAM "Prepare a Steam build (runtime is still optional)" OFF)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(PNG REQUIRED)

find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    set(ARENA_SDL2_TARGET SDL2::SDL2)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(ARENA_SDL2_TARGET PkgConfig::SDL2)
endif()

set(ARENA2004_GAME_SOURCES
    src/gl_util.c
    src/ui.c
    src/world.c
    src/render.c
    src/game.c
    src/hud.c
    src/audio.c
    src/editor.c
    src/net.c
    src/console.c
    src/steam_bridge.c
    src/main.c
)

add_executable(arena2004 ${ARENA2004_GAME_SOURCES})
target_include_directories(arena2004 PRIVATE src)
if(WIN32)
    target_link_libraries(arena2004 PRIVATE ${ARENA_SDL2_TARGET} GLEW::GLEW OpenGL::GL PNG::PNG)
else()
    target_link_libraries(arena2004 PRIVATE ${ARENA_SDL2_TARGET} GLEW::GLEW OpenGL::GL PNG::PNG m)
endif()

if(WIN32)
    target_link_libraries(arena2004 PRIVATE ws2_32)
    target_compile_definitions(arena2004 PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
elseif(APPLE)
    target_link_libraries(arena2004 PRIVATE "-framework OpenGL")
    target_compile_definitions(arena2004 PRIVATE _DARWIN_C_SOURCE)
else()
    target_link_libraries(arena2004 PRIVATE dl)
    target_compile_definitions(arena2004 PRIVATE _GNU_SOURCE=1)
endif()

if(ARENA2004_STEAM)
    target_compile_definitions(arena2004 PRIVATE ARENA2004_STEAM_BUILD=1)
endif()

if(ARENA2004_BUILD_LOBBY)
    add_executable(arena2004_lobby src/lobby.c)
    target_include_directories(arena2004_lobby PRIVATE src)
    if(WIN32)
        target_link_libraries(arena2004_lobby PRIVATE ws2_32)
        target_compile_definitions(arena2004_lobby PRIVATE _CRT_SECURE_NO_WARNINGS)
    elseif(NOT APPLE)
        target_compile_definitions(arena2004_lobby PRIVATE _GNU_SOURCE=1)
    endif()
    install(TARGETS arena2004_lobby RUNTIME DESTINATION bin)
endif()

install(TARGETS arena2004 RUNTIME DESTINATION bin)
install(DIRECTORY assets DESTINATION share/arena2004)
install(DIRECTORY maps DESTINATION share/arena2004)
install(FILES README.md LICENSE DESTINATION share/arena2004)

# Portable packages: cpack -G ZIP / DEB / NSIS / DragNDrop
set(CPACK_PACKAGE_NAME "Arena2004")
set(CPACK_PACKAGE_VENDOR "Arena2004")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast OpenGL arena FPS with UE3-style editor and NAT multiplayer")
set(CPACK_PACKAGE_EXECUTABLES "arena2004" "Arena2004")
include(CPack)
