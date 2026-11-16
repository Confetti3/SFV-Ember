# A data file embedded as the bytes of one NUL-ended text: in <output>,
# <name_space>::<symbol> is a `const char* const` to it, constant-initialized.
# ASCII only, which JSON data is: a byte above 0x7F would narrow in the array's
# initializer. The one way a data file of SFV Ember is embedded: CMakeLists.txt
# includes this file for the function, and scripts/build-room-host-linux.sh runs
# it as a script,
#   cmake -DSOURCE=<file> -DOUTPUT=<file.cxx> -DNAME_SPACE=<ns> -DSYMBOL=<name> -P cmake/EmbedTextFile.cmake
function(sfve_embed_text_file source output name_space symbol)
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "${source} is missing")
    endif()
    # A build re-configures when the data changes; a script run has no build.
    if(NOT CMAKE_SCRIPT_MODE_FILE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    endif()
    file(READ "${source}" bytes HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
    if(bytes MATCHES "0x[89a-f][0-9a-f],")
        message(FATAL_ERROR "${source} holds a byte above 0x7F; the embedded copy is ASCII only")
    endif()
    file(CONFIGURE OUTPUT "${output}" CONTENT
"// Generated from ${source} by cmake/EmbedTextFile.cmake.
namespace ${name_space} {
static const char kBytes[] = {${bytes}0};
extern const char* const ${symbol} = kBytes;
}
")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    foreach(required SOURCE OUTPUT NAME_SPACE SYMBOL)
        if(NOT DEFINED ${required})
            message(FATAL_ERROR "EmbedTextFile.cmake needs -D${required}=...")
        endif()
    endforeach()
    sfve_embed_text_file("${SOURCE}" "${OUTPUT}" "${NAME_SPACE}" "${SYMBOL}")
endif()
