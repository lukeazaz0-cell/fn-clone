cmake_minimum_required(VERSION 3.16)
project(StormIsland LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STORM_BUILD_CLIENT "Build the raylib game client" ON)
option(STORM_BUILD_SERVER "Build the dedicated game server" ON)
option(STORM_BUILD_BACKEND "Build the backend (accounts, lockers, matchmaking, admin panel)" ON)
option(STORM_BUILD_TESTS "Build headless simulation tests" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Put every executable in one folder so the backend can find the game server binary.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${cfg} CFG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

find_package(Threads REQUIRED)

if(MSVC)
  add_compile_options(/W3 /utf-8 /bigobj)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wno-unused-parameter)
endif()
if(WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

# ------------------------------------------------------------------ shared
add_library(storm_shared STATIC
  shared/net/udp.cpp
  shared/game/items.cpp
  shared/game/cosmetics.cpp
  shared/game/world.cpp
  shared/game/map.cpp
  shared/game/building.cpp
  shared/game/movement.cpp
)
target_include_directories(storm_shared PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/third_party ${CMAKE_SOURCE_DIR}/third_party/httplib)
target_link_libraries(storm_shared PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(storm_shared PUBLIC ws2_32)
endif()

# ------------------------------------------------------------------ game server core (also used by client offline mode)
if(STORM_BUILD_SERVER OR STORM_BUILD_CLIENT OR STORM_BUILD_TESTS)
  add_library(storm_server_core STATIC
    server/game.cpp
    server/bots.cpp
    server/netserver.cpp
    server/backend_link.cpp
    server/runner.cpp
  )
  target_link_libraries(storm_server_core PUBLIC storm_shared)
  if(WIN32)
    target_link_libraries(storm_server_core PUBLIC crypt32)
  endif()
endif()

if(STORM_BUILD_SERVER)
  add_executable(stormisland-server server/main.cpp)
  target_link_libraries(stormisland-server PRIVATE storm_server_core)
endif()

# ------------------------------------------------------------------ backend
if(STORM_BUILD_BACKEND)
  add_library(storm_sqlite STATIC third_party/sqlite/sqlite3.c)
  target_include_directories(storm_sqlite PUBLIC third_party/sqlite)
  target_compile_definitions(storm_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
  if(NOT MSVC)
    target_compile_options(storm_sqlite PRIVATE -w)
  endif()
  if(UNIX)
    target_link_libraries(storm_sqlite PUBLIC ${CMAKE_DL_LIBS})
  endif()

  add_executable(stormisland-backend
    backend/src/main.cpp
    backend/src/db.cpp
    backend/src/crypto.cpp
    backend/src/process.cpp
    backend/src/backend.cpp
  )
  target_link_libraries(stormisland-backend PRIVATE storm_shared storm_sqlite)
  if(WIN32)
    target_link_libraries(stormisland-backend PRIVATE bcrypt)
  endif()
  # Admin panel web files next to the binary
  add_custom_command(TARGET stormisland-backend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/backend/web $<TARGET_FILE_DIR:stormisland-backend>/web)
endif()

# ------------------------------------------------------------------ client
if(STORM_BUILD_CLIENT)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(raylib)

  add_executable(stormisland
    client/src/main.cpp
    client/src/app.cpp
    client/src/net_client.cpp
    client/src/api.cpp
    client/src/render_world.cpp
    client/src/gfx.cpp
    client/src/models.cpp
    client/src/render_chars.cpp
    client/src/hud.cpp
    client/src/menus.cpp
    client/src/ui.cpp
    client/src/audio.cpp
    client/src/game_client.cpp
  )
  target_link_libraries(stormisland PRIVATE storm_server_core raylib)
  # CC0 models/sounds (client/assets/CREDITS.md) next to the executable
  add_custom_command(TARGET stormisland POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/client/assets $<TARGET_FILE_DIR:stormisland>/assets)
  if(APPLE)
    target_link_libraries(stormisland PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
  endif()
  if(WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(stormisland PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
      target_link_options(stormisland PRIVATE /ENTRY:mainCRTStartup)
    endif()
  endif()
endif()

# ------------------------------------------------------------------ tests
if(STORM_BUILD_TESTS)
  enable_testing()
  add_executable(storm_tests tests/sim_tests.cpp)
  target_link_libraries(storm_tests PRIVATE storm_server_core)
  add_test(NAME sim_tests COMMAND storm_tests)
endif()
