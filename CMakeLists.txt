cmake_minimum_required(VERSION 3.25)
project(sfv_ember LANGUAGES CXX)

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

# Modules copied verbatim from SF4 Ember (docs/SHARED_FROM_EMBER.md).
add_library(sfve_shared STATIC
    src/shared/sf4e__GgpoGate.cxx
    src/shared/platform/HelperProcess.cxx
    src/shared/platform/HelperClient.cxx)
target_include_directories(sfve_shared PUBLIC src/shared)
target_compile_options(sfve_shared PRIVATE /W3)
target_link_libraries(sfve_shared PUBLIC bcrypt)

# Game-agnostic rollback core: state regions, hashing, the GGPO session and
# the synctest harness. No game, no hooks; unit tested against MockFighter.
add_library(sfve_core STATIC
    src/core/StateRegions.cxx
    src/core/RollbackSession.cxx
    src/core/SyncTest.cxx
    src/core/NetPolicy.cxx
    src/core/MatchLink.cxx)
target_include_directories(sfve_core PUBLIC src)
target_link_libraries(sfve_core PUBLIC sfve_shared ggpo::GGPO xxHash::xxhash nlohmann_json::nlohmann_json ws2_32 winmm)

add_library(sfve_testbed STATIC src/testbed/MockFighter.cxx)
target_link_libraries(sfve_testbed PUBLIC sfve_core)

add_executable(sfve_tests
    tests/main.cxx
    tests/StateRegionsTests.cxx
    tests/SyncTestTests.cxx
    tests/RollbackSessionTests.cxx
    tests/NetPolicyTests.cxx
    tests/MatchLinkTests.cxx
    tests/MatchResultTests.cxx
    src/mod/MatchResult.cxx)
target_link_libraries(sfve_tests PRIVATE sfve_testbed doctest::doctest)

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
    src/mod/HelperLink.cxx
    src/mod/GameScene.cxx
    src/mod/GameThread.cxx
    src/mod/GameHooks.cxx
    src/mod/BattleDriver.cxx
    src/mod/SyncTestMode.cxx
    src/mod/NetplayMode.cxx
    src/mod/GameInput.cxx
    src/mod/WriteWatch.cxx
    src/mod/LagSim.cxx
    src/mod/GameContent.cxx
    src/overlay/Overlay.cxx)
target_link_libraries(sfv_ember PRIVATE sfve_core minhook::minhook imgui::imgui d3d11 dxgi)

# Proxy the game loads by name from its own folder; it forwards the real
# exports and loads sfv_ember.dll. Which DLL to proxy is decided from the
# exe's import table (docs/design/SFV_PORT.md, open question 4).
add_library(sfve_proxy SHARED src/proxy/xinput_proxy.cxx src/proxy/xinput_proxy.def)
set_target_properties(sfve_proxy PROPERTIES OUTPUT_NAME xinput1_3)

# Stand-in for the game exe: proves proxy -> entry point -> mod -> guard.
add_executable(guard_probe tests/guard_probe.cxx)
target_link_libraries(guard_probe PRIVATE sfve_proxy ws2_32)
add_dependencies(guard_probe sfv_ember)

enable_testing()
add_test(NAME sfve_tests COMMAND sfve_tests)
add_test(NAME guard_probe COMMAND guard_probe WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
