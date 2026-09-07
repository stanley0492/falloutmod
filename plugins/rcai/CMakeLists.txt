# RCAI — Reactive Combat AI
# -------------------------
# F4SE plugin (M1 target of the FO4 Modernization Program, see
# docs/MODERNIZATION_ROADMAP.md).
#
# This scaffold expects the F4SE 0.7.x SDK under
#   ${CMAKE_SOURCE_DIR}/third_party/f4se/
#     include/   (f4se/ + common/ headers)
#     lib/       (f4se_64.lib import library)
# build.yml fetches it automatically in CI; for local builds see README.md.

cmake_minimum_required(VERSION 3.22)

set(F4SE_ROOT ${CMAKE_SOURCE_DIR}/third_party/f4se)

if(NOT EXISTS ${F4SE_ROOT}/include)
    message(WARNING
        "F4SE SDK not found at ${F4SE_ROOT}.\n"
        "CI (build.yml) downloads it automatically. Locally, download the latest\n"
        "F4SE release (github.com/afrocat/F4SE), unzip it, and place the\n"
        "'include' and 'lib' directories under third_party/f4se/.")
endif()

add_library(rcai SHARED
    src/RCAI.cpp
)

set_target_properties(rcai PROPERTIES
    OUTPUT_NAME "RCAI"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(rcai PRIVATE ${F4SE_ROOT}/include)
target_link_libraries(rcai PRIVATE ${F4SE_ROOT}/lib/f4se_64.lib)

if(MSVC)
    target_compile_options(rcai PRIVATE /W4 /utf-8 /MP)
    # The plugin must not drag in the default CRT DLLs the game doesn't ship.
    target_compile_definitions(rcai PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
endif()
