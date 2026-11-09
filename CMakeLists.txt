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
find_package(CLI11 CONFIG REQUIRED)
# Detours starts the game with the mod in its imports (src/launcher/SfvLaunch.cxx).
find_path(DETOURS_INCLUDE_DIR detours/detours.h REQUIRED)
find_library(DETOURS_LIBRARY detours REQUIRED)

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
# SFV Ember's package files (common/PackageInventory.hxx in the library):
# what Updater.exe and the update check accept in a package.
set(EMBER_PACKAGE_INVENTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher/PackageInventory.inc")
add_subdirectory("${SFVE_EMBER_SOURCE_ROOT}/cmake/ember-x64-probe" "${CMAKE_BINARY_DIR}/ember")

# Game-agnostic rollback core: state regions, hashing, the GGPO session and
# the synctest harness. No game, no hooks; unit tested against MockFighter.
add_library(sfve_core STATIC
    src/core/StateRegions.cxx
    src/core/ReplayFile.cxx
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
    tests/WatcherTests.cxx
    tests/ReplayTests.cxx
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
    src/ember/SfvReplays.cxx
    src/ember/SfvSelection.cxx
    src/ember/SfvSelectionScreen.cxx
    src/ember/SfvShell.cxx
    src/ember/SfvTraining.cxx
    src/ember/SfvVerification.cxx
    src/overlay/Overlay.cxx
    src/overlay/TextureUploaderD3D11.cxx)
# SFV Ember's identity (SfvProduct.cxx) with its own header mark (art/brand,
# scripts/build-sfv-brand.py), in a library of its own: the mod, the
# launcher, the updater and the tests all link the one definition.
include("${SFVE_EMBER_SOURCE_ROOT}/cmake/sf4e_embed.cmake")
add_library(sfve_product STATIC src/ember/SfvProduct.cxx)
target_include_directories(sfve_product PRIVATE "${SFVE_EMBER_SOURCE_ROOT}/src")
# SFV Ember's own words over the shared catalogs (locales/<tag>.po), one
# catalog for every language the library declares in its locales.json: a
# language without one stops the configure. Each is embedded as a raw string
# (SfvLocaleOverlays.inc), so the product's overlay table stays
# constant-initialized.
# (The library's own cmake/sf4e_locales.cmake already re-runs the configure
# when locales.json changes; naming it here too makes ninja refuse it.)
set(sfve_locale_registry "${SFVE_EMBER_SOURCE_ROOT}/locales/locales.json")
file(READ "${sfve_locale_registry}" sfve_locale_json)
string(JSON sfve_locale_count LENGTH "${sfve_locale_json}" locales)
math(EXPR sfve_locale_last "${sfve_locale_count} - 1")
set(sfve_locale_texts "")
set(sfve_locale_rows "")
foreach(index RANGE ${sfve_locale_last})
    string(JSON tag GET "${sfve_locale_json}" locales ${index} tag)
    set(catalog "${CMAKE_CURRENT_SOURCE_DIR}/locales/${tag}.po")
    if(NOT EXISTS "${catalog}")
        message(FATAL_ERROR "SFV Ember has no catalog for ${tag}, a language the Ember library declares: add locales/${tag}.po")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${catalog}")
    file(READ "${catalog}" text)
    # A checkout may give the file CRLF line ends.
    string(REPLACE "\r\n" "\n" text "${text}")
    # The text goes into a raw literal through file(GENERATE): one that ends
    # the literal or reads as a generator expression would change it.
    if(text MATCHES "\\)po\"" OR text MATCHES "\\$<")
        message(FATAL_ERROR "locales/${tag}.po holds )po\" or $<, which the embedded catalog cannot carry")
    endif()
    # One raw literal per entry, concatenated: MSVC takes at most about 16 KB
    # in one literal, which a whole Cyrillic or CJK catalog can pass.
    string(REPLACE "\n\nmsgid " "\n\n)po\"\n    R\"po(msgid " text "${text}")
    string(MAKE_C_IDENTIFIER "${tag}" name)
    string(APPEND sfve_locale_texts "const char kOverlay_${name}[] = R\"po(${text})po\";\n")
    string(APPEND sfve_locale_rows "    {\"${tag}\", kOverlay_${name}},\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/locales/SfvLocaleOverlays.inc" CONTENT
    "// Generated from locales/*.po by CMakeLists.txt (sfve_product).\n${sfve_locale_texts}\nconst ember::LocaleOverlay LocaleOverlays[] = {\n${sfve_locale_rows}    {nullptr, nullptr}};\n")
target_include_directories(sfve_product PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/locales")
unset(text)
unset(tag)
unset(name)
unset(catalog)
unset(index)
sf4e_embed_brand_mark(sfve_product "${CMAKE_CURRENT_SOURCE_DIR}/art/brand/sfv-ember-mark.rgba")
target_link_libraries(sfve_ember PUBLIC sfve_core sfve_product ember_runtime_x64 minhook::minhook imgui::imgui d3d11 dxgi)

# The runtime under SFV's definitions, with nothing of SF4's roster, stages
# or custom content: SFV's catalog (SfvCatalog.cxx) answers every pick check.
add_executable(sfve_ember_tests
    tests/main.cxx
    tests/EmberAdapterTests.cxx
    tests/BattleIdentityTests.cxx
    tests/HudLayoutTests.cxx
    tests/SfvReplaysTests.cxx
    tests/CrashRecordTests.cxx
    tests/LocaleOverlayTests.cxx
    tests/VirtualPadsTests.cxx
    src/mod/ControlPipe.cxx
    src/mod/CrashRecord.cxx
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
target_link_libraries(sfve_ember_tests PRIVATE sfve_ember sfve_testbed minhook::minhook doctest::doctest dbghelp)
# LocaleOverlayTests reads the library's fonts and locales.json from its checkout.
file(TO_CMAKE_PATH "${SFVE_EMBER_SOURCE_ROOT}" sfve_ember_root_forward)
target_compile_definitions(sfve_ember_tests PRIVATE SFVE_EMBER_SOURCE_ROOT="${sfve_ember_root_forward}")

# The in-game mod. Loads inert: it blocks every non-loopback connection first,
# then does nothing else unless the exe is the exact build it was made for.
add_library(sfv_ember SHARED
    src/mod/dllmain.cxx
    src/mod/EarlyStart.cxx
    src/mod/StartClaim.cxx
    src/mod/sfv_ember.def
    src/mod/Log.cxx
    src/mod/MatchEnd.cxx
    src/mod/MatchResult.cxx
    src/mod/ModConfig.cxx
    src/mod/ControlPipe.cxx
    src/mod/CrashRecord.cxx
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
target_link_libraries(sfv_ember PRIVATE sfve_ember dbghelp)
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

# Launcher.exe and Updater.exe: Ember's launcher and updater programs (the
# library's launcher.cxx and updater.cxx, ember_launcher_x64 and
# ember_updater_x64) under SFV Ember's product, its launch steps and its
# package inventory (docs/design/EMBER_LIBRARY.md section 22).
set(SFVE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/art/brand/sfv-ember.ico")
set(SFVE_VERSION_COMMAS "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
function(sfve_program_resources target description file_name)
    set(SFVE_FILE_DESCRIPTION "${description}")
    set(SFVE_FILE_NAME "${file_name}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/launcher/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}.rc" @ONLY)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}.rc")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}.rc" APPEND PROPERTY OBJECT_DEPENDS "${SFVE_ICON}")
endfunction()
add_executable(Launcher WIN32
    src/launcher/SfvLaunch.cxx
    src/launcher/LauncherSelection.cxx
    src/launcher/Launcher.exe.manifest)
target_include_directories(Launcher PRIVATE "${SFVE_EMBER_SOURCE_ROOT}/src" "${SFVE_EMBER_SOURCE_ROOT}/src/common"
    "${DETOURS_INCLUDE_DIR}")
target_compile_definitions(Launcher PRIVATE UNICODE _UNICODE)
# Its wWinMain is in ember_launcher_x64, where the linker does not look for an entry point.
target_link_options(Launcher PRIVATE "/ENTRY:wWinMainCRTStartup")
target_link_libraries(Launcher PRIVATE sfve_product ember_launcher_x64 CLI11::CLI11 "${DETOURS_LIBRARY}")
sfve_program_resources(Launcher "SFV Ember" "Launcher.exe")
# A Windows program, as SF4 Ember's: its progress window is all a player sees.
add_executable(Updater WIN32 src/launcher/LauncherSelection.cxx)
target_include_directories(Updater PRIVATE "${SFVE_EMBER_SOURCE_ROOT}/src" "${SFVE_EMBER_SOURCE_ROOT}/src/common")
target_link_options(Updater PRIVATE "/ENTRY:wmainCRTStartup")
target_link_libraries(Updater PRIVATE sfve_product ember_updater_x64)
sfve_program_resources(Updater "SFV Ember Updater" "Updater.exe")
# Discord's companion (ember-discord.exe, the library's src/discord): the
# player's activity and invitations. SFV Ember uses SF4 Ember's Discord
# application (the maintainer, 2026-11-08; EMBER_LIBRARY.md section 33): the library's
# cmake/discord-sdk-pin.json gives its id, the SDK version and the archive's
# hash. Built only from that archive (SFVE_DISCORD_SDK_ARCHIVE, checked
# against the pin); without it there is no companion, and scripts/package.ps1
# refuses to make a package.
set(SFVE_DISCORD_SDK_ARCHIVE "" CACHE FILEPATH "The Discord Social SDK archive the Ember library's discord-sdk-pin.json names")
if(SFVE_DISCORD_SDK_ARCHIVE)
    file(READ "${SFVE_EMBER_SOURCE_ROOT}/cmake/discord-sdk-pin.json" sfve_discord_pin)
    string(JSON sfve_discord_app GET "${sfve_discord_pin}" applicationId)
    string(JSON sfve_discord_version GET "${sfve_discord_pin}" version)
    string(JSON sfve_discord_hash GET "${sfve_discord_pin}" sha256)
    string(TOLOWER "${sfve_discord_hash}" sfve_discord_hash)
    file(SHA256 "${SFVE_DISCORD_SDK_ARCHIVE}" sfve_discord_archive_hash)
    if(NOT sfve_discord_archive_hash STREQUAL sfve_discord_hash)
        message(FATAL_ERROR "The Discord SDK archive's SHA-256 does not match the Ember library's pin")
    endif()
    include("${SFVE_EMBER_SOURCE_ROOT}/cmake/sf4e_verified_unpack.cmake")
    include("${SFVE_EMBER_SOURCE_ROOT}/cmake/sf4e_discord_sdk_files.cmake")
    sf4e_unpack_verified("${SFVE_DISCORD_SDK_ARCHIVE}" "${sfve_discord_archive_hash}" "${CMAKE_BINARY_DIR}/discord-sdk"
        ${SF4E_DISCORD_SDK_FILES})
    set(sfve_discord_sdk "${CMAKE_BINARY_DIR}/discord-sdk/discord_social_sdk")
    add_executable(ember-discord WIN32 "${SFVE_EMBER_SOURCE_ROOT}/src/discord/main.cxx")
    target_compile_definitions(ember-discord PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        SF4E_DISCORD_APPLICATION_ID=${sfve_discord_app}ULL)
    target_include_directories(ember-discord PRIVATE "${sfve_discord_sdk}/include")
    target_link_libraries(ember-discord PRIVATE sfve_product nlohmann_json::nlohmann_json
        "${sfve_discord_sdk}/lib/release/discord_partner_sdk.lib" advapi32 shell32)
    sfve_program_resources(ember-discord "SFV Ember Discord companion" "ember-discord.exe")
    # Beside it in the build tree, as in a package: the SDK's DLL and notice,
    # and what it was built with (the library's discord-build.json form).
    add_custom_command(TARGET ember-discord POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${sfve_discord_sdk}/bin/release/discord_partner_sdk.dll" "$<TARGET_FILE_DIR:ember-discord>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${sfve_discord_sdk}/License-Notices.txt" "$<TARGET_FILE_DIR:ember-discord>/Discord-SDK-notices.txt")
    file(WRITE "${CMAKE_BINARY_DIR}/discord-build.json"
        "{\"applicationId\":\"${sfve_discord_app}\",\"sdkVersion\":\"${sfve_discord_version}\",\"sdkSha256\":\"${sfve_discord_hash}\"}\n")
else()
    message(STATUS "No Discord SDK archive (SFVE_DISCORD_SDK_ARCHIVE): SFV Ember is built without its Discord companion")
endif()
# The updater's own validation and extraction, for scripts/package.ps1 and the
# library's scripts/test-updater-download-validation.ps1.
add_executable(sfve_package_check tests/package_check.cxx src/launcher/LauncherSelection.cxx)
target_include_directories(sfve_package_check PRIVATE "${SFVE_EMBER_SOURCE_ROOT}/src" "${SFVE_EMBER_SOURCE_ROOT}/src/common")
target_link_options(sfve_package_check PRIVATE "/ENTRY:wmainCRTStartup")
target_link_libraries(sfve_package_check PRIVATE sfve_product ember_neutral_x64 nlohmann_json::nlohmann_json bcrypt shell32)

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
