# SFV Ember's x64 Windows build on a Linux build PC (scripts/build.ps1 on Linux; docs/release/PUBLISHING.md,
# "The build PC"): clang-cl, lld-link, llvm-lib, llvm-rc and llvm-mt against the Microsoft CRT and Windows SDK
# that xwin splats. SF4E_XWIN names the splat and SF4E_LLVM_BIN the LLVM tools. vcpkg's ports are built with it
# too (build.ps1's triplet chainloads it); inside a port's build (VCPKG_CRT_LINKAGE is defined) warnings are off,
# while SFV Ember and the Ember library keep their own /W4 and /WX.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
# The splat has no debug CRT libraries, and try_compile defaults to Debug.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(NOT DEFINED ENV{SF4E_XWIN} OR NOT DEFINED ENV{SF4E_LLVM_BIN})
    message(FATAL_ERROR "clang-cl-x64.cmake needs SF4E_XWIN (the xwin splat) and SF4E_LLVM_BIN (the LLVM tools)")
endif()
set(_xwin "$ENV{SF4E_XWIN}")
set(_llvm "$ENV{SF4E_LLVM_BIN}")

set(CMAKE_C_COMPILER "${_llvm}/clang-cl")
set(CMAKE_CXX_COMPILER "${_llvm}/clang-cl")
set(CMAKE_LINKER "${_llvm}/lld-link")
set(CMAKE_AR "${_llvm}/llvm-lib")
set(CMAKE_RC_COMPILER "${_llvm}/llvm-rc")
set(CMAKE_MT "${_llvm}/llvm-mt")

set(_inc "/imsvc ${_xwin}/crt/include /imsvc ${_xwin}/sdk/include/ucrt /imsvc ${_xwin}/sdk/include/um /imsvc ${_xwin}/sdk/include/shared /imsvc ${_xwin}/sdk/include/winrt")
# The toolchain's own folders as neutral names wherever a path of theirs is compiled in (__FILE__ in a header).
set(_maps "/clang:-ffile-prefix-map=${_xwin}=/xwin /clang:-ffile-prefix-map=${_llvm}=/llvm")
if(DEFINED VCPKG_CRT_LINKAGE)
    set(_quiet "/w ")
else()
    set(_quiet "")
endif()
set(CMAKE_C_FLAGS_INIT "--target=x86_64-pc-windows-msvc ${_quiet}${_inc} ${_maps}")
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-pc-windows-msvc ${_quiet}${_inc} ${_maps}")
set(_libs "/libpath:${_xwin}/crt/lib/x86_64 /libpath:${_xwin}/sdk/lib/ucrt/x86_64 /libpath:${_xwin}/sdk/lib/um/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_RC_FLAGS_INIT "-DWIN32 /I ${_xwin}/sdk/include/um /I ${_xwin}/sdk/include/shared /I ${_xwin}/sdk/include/ucrt /I ${_xwin}/crt/include")
