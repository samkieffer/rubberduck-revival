cmake_minimum_required(VERSION 3.22)
project(RubberDuckRevival VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RD_BUILD_PLUGIN "Build the JUCE plugin (AU/VST3/Standalone)" ON)
option(RD_BUILD_TOOLS "Build command-line tools" ON)

# Engine: no dependencies. Bit-exactness relies on IEEE double intermediates
# without fused multiply-add, so contraction is disabled explicitly (clang on
# Apple Silicon would otherwise fuse a*b+c).
add_library(rdcore STATIC
    core/src/Delay.cpp
    core/src/Engine.cpp
    core/src/H30File.cpp
    core/src/LadderFilter.cpp
    core/src/Mixing.cpp
    core/src/Shaper.cpp
    core/src/StateVariableFilter.cpp
    core/src/Tables.cpp)
target_include_directories(rdcore PUBLIC core/include)
if(MSVC)
    target_compile_options(rdcore PRIVATE /fp:precise)
else()
    target_compile_options(rdcore PRIVATE -ffp-contract=off -fno-fast-math)
endif()
set_target_properties(rdcore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(RD_BUILD_TOOLS)
    add_executable(rdrender tools/rdrender/main.cpp)
    target_link_libraries(rdrender PRIVATE rdcore)
    add_executable(rdcheck tools/rdcheck/main.cpp)
    target_link_libraries(rdcheck PRIVATE rdcore)

    # Bit-exactness against the original. Needs the installer's content folder
    # (not in the repository): cmake -DRD_CONTENT_DIR=/path/to/content
    enable_testing()
    # Synthetic patterns shipped with the repository, hashes recorded from the original.
    add_test(NAME bit_exact_fixtures
             COMMAND rdcheck "${CMAKE_SOURCE_DIR}/tests/fixtures" "${CMAKE_SOURCE_DIR}/tests/fixtures/hashes.txt")
    # The installer's own patterns (not in the repository): -DRD_CONTENT_DIR=/path/to/content
    set(RD_CONTENT_DIR "${CMAKE_SOURCE_DIR}/reference/content" CACHE PATH "Original pattern files")
    if(EXISTS "${RD_CONTENT_DIR}/Acid_was_my_first_love.h30")
        add_test(NAME bit_exact_installer
                 COMMAND rdcheck "${RD_CONTENT_DIR}" "${CMAKE_SOURCE_DIR}/tests/reference_hashes.txt")
    endif()
endif()

if(RD_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
