cmake_minimum_required(VERSION 3.25)
project(sfv_ember VERSION 0.1.0 LANGUAGES CXX)

# SFV Ember's own version: its log's first line, and the version Ember's
# shell shows (the library's SF4E_APP_VERSION, EMBER_APP_VERSION below). A
# pre-release until SFV Ember ships, so the shell calls it a Beta.
set(SFVE_VERSION "${PROJECT_VERSION}-beta.1")

# Street Fighter V is a 64-bit game: everything here builds for x64 only.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SFV Ember builds for x64 only (use the x64-windows-static-md triplet)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

find_package(ggpo CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

# Ember, the library SFV Ember is built on (docs/design/EMBER_LIBRARY.md,
# sections 4 and 9): its game-neutral code and its runtime, compiled from the
# library's sources with its own x64 build, the one its probe checks. Until
# the library has its own repository, the build names its checkout
# (scripts/build.ps1 -EmberRoot).
set(SFVE_EMBER_SOURCE_ROOT "" CACHE PATH "The Ember library checkout (the feat/ember-library tree)")
if(NOT EXISTS "${SFVE_EMBER_SOURCE_ROOT}/cmake/ember-x64-probe/CMakeLists.txt")
    message(FATAL_ERROR "SFVE_EMBER_SOURCE_ROOT must name the Ember library checkout "
        "(docs/design/EMBER_LIBRARY.md section 4); got '${SFVE_EMBER_SOURCE_ROOT}'")
endif()
set(EMBER_APP_VERSION "${SFVE_VERSION}")
add_subdirectory("${SFVE_EMBER_SOURCE_ROOT}/cmake/ember-x64-probe" "${CMAKE_BINARY_DIR}/ember")

# Game-agnostic rollback core: state regions, hashing, the GGPO session and
# the synctest harness. No game, no hooks; unit tested against MockFighter.
add_library(sfve_core STATIC
    src/core/StateRegions.cxx
    src/core/RollbackSession.cxx
    src/core/SyncTest.cxx
    src/core/NetPolicy.cxx)
# Ember's GGPO gate, pacing, state hash and helper client come from the
# library: its common/ folder by file name, the rest by folder.
target_include_directories(sfve_core PUBLIC src "${SFVE_EMBER_SOURCE_ROOT}/src/common" "${SFVE_EMBER_SOURCE_ROOT}/src")
target_link_libraries(sfve_core PUBLIC ember_neutral_x64 bcrypt ggpo::GGPO xxHash::xxhash nlohmann_json::nlohmann_json ws2_32 winmm)

add_library(sfve_testbed STATIC src/testbed/MockFighter.cxx)
target_link_libraries(sfve_testbed PUBLIC sfve_core)

add_executable(sfve_tests
    tests/main.cxx
    tests/StateRegionsTests.cxx
    tests/SyncTestTests.cxx
    tests/RollbackSessionTests.cxx
    tests/NetPolicyTests.cxx
    tests/MatchResultTests.cxx
    tests/RematchDecisionTests.cxx
    src/mod/MatchResult.cxx)
target_link_libraries(sfve_tests PRIVATE sfve_testbed doctest::doctest)

# SFV's game definitions for Ember's runtime (docs/design/EMBER_LIBRARY.md
# sections 10-13): the game adapter and Game(), the catalog, the product, the
# desync check, the pick labels and pictures, the runtime's host in the mod
# (EmberHost) with its test commands (RoomCommands), and Ember's shell over
# the game (SfvShell, its pick and pick screen) on the mod's Direct3D 11
# overlay.
add_library(sfve_ember STATIC
    src/ember/EmberHost.cxx
    src/ember/PublicCommands.cxx
    src/ember/RoomBattle.cxx
    src/ember/RoomCommands.cxx
    src/ember/SfvArt.cxx
    src/ember/SfvCatalog.cxx
    src/ember/SfvGame.cxx
    src/ember/SfvHud.cxx
    src/ember/SfvPick.cxx
    src/ember/SfvPickSpace.cxx
    src/ember/SfvProduct.cxx
    src/ember/SfvSelection.cxx
    src/ember/SfvSelectionScreen.cxx
    src/ember/SfvShell.cxx
    src/ember/SfvVerification.cxx
    src/overlay/Overlay.cxx
    src/overlay/TextureUploaderD3D11.cxx)
# SFV Ember's own header mark (art/brand, scripts/build-sfv-brand.py).
include("${SFVE_EMBER_SOURCE_ROOT}/cmake/sf4e_embed.cmake")
sf4e_embed_brand_mark(sfve_ember "${CMAKE_CURRENT_SOURCE_DIR}/art/brand/sfv-ember-mark.rgba")
target_link_libraries(sfve_ember PUBLIC sfve_core ember_runtime_x64 minhook::minhook imgui::imgui d3d11 dxgi)

# The runtime under SFV's definitions, with nothing of SF4's roster, stages
# or custom content: SFV's catalog (SfvCatalog.cxx) answers every pick check.
add_executable(sfve_ember_tests
    tests/main.cxx
    tests/EmberAdapterTests.cxx
    tests/BattleIdentityTests.cxx
    tests/HudLayoutTests.cxx
    src/mod/ControlPipe.cxx
    src/mod/GameArt.cxx
    src/mod/GameContent.cxx
    src/mod/GameExit.cxx
    src/mod/GameHooks.cxx
    src/mod/GameScene.cxx
    src/mod/GameThread.cxx
    src/mod/HudText.cxx
    src/mod/Log.cxx
    src/mod/MatchEnd.cxx
    src/mod/MatchResult.cxx
    src/mod/ModConfig.cxx
    src/mod/SfvAdapter.cxx
    src/mod/SfvFlow.cxx
    src/mod/SfvRegionNames.cxx
    src/mod/VersusLaunch.cxx
    src/mod/VirtualPads.cxx)
target_link_libraries(sfve_ember_tests PRIVATE sfve_ember sfve_testbed minhook::minhook doctest::doctest)

# The in-game mod. Loads inert: it blocks every non-loopback connection first,
# then does nothing else unless the exe is the exact build it was made for.
add_library(sfv_ember SHARED
    src/mod/dllmain.cxx
    src/mod/Log.cxx
    src/mod/MatchEnd.cxx
    src/mod/MatchResult.cxx
    src/mod/ModConfig.cxx
    src/mod/ControlPipe.cxx
    src/mod/VersusLaunch.cxx
    src/mod/VirtualPads.cxx
    src/mod/NetGuard.cxx
    src/mod/OfflineDialogs.cxx
    src/mod/BuildCheck.cxx
    src/mod/BackgroundPlay.cxx
    src/mod/MultiInstance.cxx
    src/mod/SfvAdapter.cxx
    src/mod/SfvRegionNames.cxx
    src/mod/ProjectileRelease.cxx
    src/mod/SfvFlow.cxx
    src/mod/FramePacer.cxx
    src/mod/GameScene.cxx
    src/mod/GameThread.cxx
    src/mod/GameHooks.cxx
    src/mod/BattleDriver.cxx
    src/mod/SyncTestMode.cxx
    src/mod/NetplayMode.cxx
    src/mod/GameInput.cxx
    src/mod/WriteWatch.cxx
    src/mod/LagSim.cxx
    src/mod/GameArt.cxx
    src/mod/GameContent.cxx
    src/mod/GameExit.cxx
    src/mod/HudText.cxx)
target_link_libraries(sfv_ember PRIVATE sfve_ember)
target_compile_definitions(sfv_ember PRIVATE SFVE_VERSION="${SFVE_VERSION}")
# Ember's own pictures the shell draws beside the mod (overlay/Overlay.hxx):
# the button prompts and the flags, and SFV Ember's own menu backdrop
# (assets/brand) in place of Ember's. SFV's fighter and stage pictures are the
# game's own, drawn from its textures at run time (src/ember/SfvArt), so no
# selection art comes along.
foreach(_folder input flags)
    add_custom_command(TARGET sfv_ember POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${SFVE_EMBER_SOURCE_ROOT}/assets/${_folder}" "${CMAKE_BINARY_DIR}/assets/${_folder}")
endforeach()
add_custom_command(TARGET sfv_ember POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand" "${CMAKE_BINARY_DIR}/assets/brand")

# Proxy the game loads by name from its own folder; it forwards the real
# exports and loads sfv_ember.dll. Which DLL to proxy is decided from the
# exe's import table (docs/design/SFV_PORT.md, open question 4).
add_library(sfve_proxy SHARED src/proxy/xinput_proxy.cxx src/proxy/xinput_proxy.def)
set_target_properties(sfve_proxy PROPERTIES OUTPUT_NAME xinput1_3)

# Stand-in for the game exe: proves proxy -> entry point -> mod -> guard.
add_executable(guard_probe tests/guard_probe.cxx)
target_link_libraries(guard_probe PRIVATE sfve_proxy ws2_32)
add_dependencies(guard_probe sfv_ember)

# SFV's room host (docs/design/EMBER_LIBRARY.md section 24): the library's
# public room host (server/roomhost) with SFV's catalog in place of SF4's. It
# judges picks by the pick space the game exported (the pipe's
# `catalog-export`, kept in data/pick-space.json), embedded here as bytes,
# and hosts only the build that pick space makes. scripts/build-room-host-linux.sh
# builds the same for the server.
set(SFVE_PICK_SPACE "${CMAKE_CURRENT_SOURCE_DIR}/data/pick-space.json")
if(NOT EXISTS "${SFVE_PICK_SPACE}")
    message(FATAL_ERROR "data/pick-space.json is missing: export it from the game (the pipe's catalog-export)")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SFVE_PICK_SPACE}")
file(READ "${SFVE_PICK_SPACE}" _sfve_pick_space HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _sfve_pick_space "${_sfve_pick_space}")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/SfvHostPickSpace.cxx" CONTENT
"// Generated from data/pick-space.json by CMakeLists.txt.
namespace sfve { namespace roomhost {
static const char kBytes[] = {${_sfve_pick_space}0};
extern const char* const kPickSpaceJson = kBytes;
} }
")
set(SFVE_ROOM_HOST_SOURCES
    "${SFVE_EMBER_SOURCE_ROOT}/server/roomhost/main.cxx"
    "${SFVE_EMBER_SOURCE_ROOT}/server/roomhost/RoomHost.cxx"
    "${SFVE_EMBER_SOURCE_ROOT}/server/roomhost/RoomHostStatus.cxx"
    "${SFVE_EMBER_SOURCE_ROOT}/server/roomhost/RoomHostHelperWindows.cxx")
add_library(sfve_room_host_catalog STATIC
    src/roomhost/SfvHostCatalog.cxx
    src/ember/SfvPickSpace.cxx
    "${CMAKE_BINARY_DIR}/generated/SfvHostPickSpace.cxx")
target_include_directories(sfve_room_host_catalog PUBLIC src "${SFVE_EMBER_SOURCE_ROOT}/src" "${SFVE_EMBER_SOURCE_ROOT}/server/roomhost")
target_link_libraries(sfve_room_host_catalog PUBLIC ember_neutral_x64 xxHash::xxhash nlohmann_json::nlohmann_json bcrypt ws2_32)
add_executable(sfve-room-host ${SFVE_ROOM_HOST_SOURCES})
target_link_libraries(sfve-room-host PRIVATE sfve_room_host_catalog)

# The room host's catalog without the game: the exported pick space is the
# build it names, and its rules refuse what SFV cannot play.
add_executable(sfve_room_host_tests tests/main.cxx tests/RoomHostCatalogTests.cxx
    "${SFVE_EMBER_SOURCE_ROOT}/server/roomhost/RoomHost.cxx" "${SFVE_EMBER_SOURCE_ROOT}/server/roomhost/RoomHostStatus.cxx")
target_link_libraries(sfve_room_host_tests PRIVATE sfve_room_host_catalog doctest::doctest)

enable_testing()
add_test(NAME sfve_tests COMMAND sfve_tests)
# Ember's runtime under the library's test game, built with SFV Ember's packages.
add_test(NAME ember_runtime COMMAND ember_runtime_x64_test)
# SFV's game definitions, and the runtime linked under them.
add_test(NAME sfve_ember_tests COMMAND sfve_ember_tests)
# SFV's room host catalog over the exported pick space.
add_test(NAME sfve_room_host_tests COMMAND sfve_room_host_tests)
add_test(NAME guard_probe COMMAND guard_probe WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
