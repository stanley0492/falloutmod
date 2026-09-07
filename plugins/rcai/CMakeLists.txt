# RCAI — Reactive Combat AI
# -------------------------
# F4SE plugin (M1–M3) + the headless test/benchmark suite that proves the AI
# stack (M1/M2 acceptance) without the game.
#
# Targets (see docs/MODERNIZATION_ROADMAP.md, milestones M1–M3):
#   rcai        — the F4SE plugin DLL (Windows/MSVC only)
#   rcai_tests  — unit tests for perception/threat/cover/squad/behaviour/
#                 watchdog/tuner/faction-memory (any platform, any CI)
#   rcai_bench  — the M2 AI scoreboard (cover usage, flanking, TTK)
#
# The F4SE plugin expects the F4SE 0.7.x SDK under
#   ${CMAKE_SOURCE_DIR}/third_party/f4se/{include,lib}
# build.yml fetches it automatically in CI; for local builds see README.md.

cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT MSVC)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# headless core: tests + benchmark (build everywhere)
# ---------------------------------------------------------------------------

add_executable(rcai_tests
    src/world/f4se_sampler.cpp
    tests/main.cpp
    tests/test_util.cpp
    tests/test_perception.cpp
    tests/test_behaviour.cpp
    tests/test_systems.cpp
    tests/test_f4se_sampler.cpp
)
add_executable(rcai_bench bench/bench_ai.cpp)

target_include_directories(rcai_tests PRIVATE src .)
target_include_directories(rcai_bench PRIVATE src .)

if(MSVC)
    target_compile_options(rcai_tests PRIVATE /W4 /utf-8 /MP)
    target_compile_options(rcai_bench PRIVATE /W4 /utf-8 /MP)
else()
    target_compile_options(rcai_tests PRIVATE -Wall -Wextra -O2)
    target_compile_options(rcai_bench PRIVATE -Wall -Wextra -O2)
endif()

# ---------------------------------------------------------------------------
# the F4SE plugin DLL (Windows only — the game is a Windows/Next-Gen target)
# ---------------------------------------------------------------------------

if(WIN32)
    set(F4SE_ROOT ${CMAKE_SOURCE_DIR}/third_party/f4se)
    if(NOT EXISTS ${F4SE_ROOT}/include)
        message(WARNING
            "F4SE SDK not found at ${F4SE_ROOT}. "
            "CI (build.yml) downloads it automatically; locally, place the "
            "F4SE release 'include' and 'lib' folders under third_party/f4se/.")
    endif()

    add_library(rcai SHARED
        src/RCAI.cpp
        src/world/f4se_sampler.cpp
    )
    set_target_properties(rcai PROPERTIES OUTPUT_NAME "RCAI")
    target_include_directories(rcai PRIVATE src . ${F4SE_ROOT}/include)
    target_link_libraries(rcai PRIVATE ${F4SE_ROOT}/lib/f4se_64.lib)
    target_compile_definitions(rcai PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
    )
    target_compile_options(rcai PRIVATE /W4 /utf-8 /MP)
endif()
