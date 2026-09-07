# FO4 Modernization Program — root build
# ----------------------------------------
# CI entry point (build.yml): builds build/Release/RCAI.dll and packages
# it with config/RCAI.ini into a GitHub release on version tags.
#
# Local build (Windows): see README.md → "Building the plugin".

cmake_minimum_required(VERSION 3.22)

project(falloutmod CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Match build.yml: the Release DLL lands at <build>/Release/RCAI.dll
# (multi-config MSVC appends the config name).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(MSVC)
	add_compile_options(/utf-8)
endif()

add_subdirectory(plugins/rcai)
