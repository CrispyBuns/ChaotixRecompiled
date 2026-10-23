option(CHAOTIX_BUILD_FRONTEND "Build the SDL3 frontend" ON)
option(CHAOTIX_BUILD_TESTS "Build unit / validation tests" ON)
option(CHAOTIX_LONG_TESTS "Register long-running validation tests (minutes)" OFF)
option(CHAOTIX_BUILD_TOOLS "Build host tools (rom_analyzer, recompiler)" ON)
set(CHAOTIX_ROM "" CACHE FILEPATH "Path to the user's Knuckles' Chaotix ROM (never committed)")
set(CHAOTIX_SDL3_SOURCE_DIR "" CACHE PATH "Build SDL3 from this source tree instead of find_package (mobile)")
set(CHAOTIX_SDL3_TTF_SOURCE_DIR "" CACHE PATH "Build SDL3_ttf from this source tree instead of find_package (mobile)")
set(CHAOTIX_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated" CACHE PATH "Directory holding recompiler output")

function(chaotix_set_warnings target)
    if(MSVC)
        # /utf-8: the sources are UTF-8 (they contain em dashes and the like),
        # which MSVC otherwise reads in the machine's ANSI code page.
        # _CRT_SECURE_NO_WARNINGS: the code uses standard fopen/getenv on
        # purpose, since it has to build with GCC and Clang too.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
        # RG35XX-H uses Cortex-A35 cores. Tune ARM64 builds for that CPU so
        # the SH-2 recompiled code, interpreter, and software 32X paths can
        # benefit from target-specific instruction selection and scheduling.
        # Fast-math is restricted to Release builds so debug/validation builds
        # retain normal floating-point semantics.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|AARCH64)$"
           AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
            target_compile_options(${target} PRIVATE
                -mcpu=cortex-a35
                -mtune=cortex-a35
                "$<$<CONFIG:Release>:-ffast-math>"
            )

            # The generated SH-2/M68K game code lives in chaotix_game. Give
            # that hot path an explicit Release optimization level and omit
            # frame pointers to reduce register pressure on the Cortex-A35.
            if(target STREQUAL "chaotix_game")
                target_compile_options(${target} PRIVATE
                    "$<$<CONFIG:Release>:-O3;-fomit-frame-pointer;-fno-semantic-interposition>"
                )
            endif()
        endif()
    endif()
endfunction()
