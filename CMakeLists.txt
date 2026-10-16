cmake_minimum_required( VERSION 4.0.0 )

list( APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" )

include( cdrake )

set( CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version." )
set( CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architecture." )

project( bg3ae-macos
         VERSION 1.0.0
         DESCRIPTION "Keeps Baldur's Gate 3 achievements enabled with mods on Apple Silicon Macs."
         LANGUAGES CXX )

set( CMAKE_CXX_STANDARD 23 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

if( NOT APPLE )
    message( FATAL_ERROR "This tool patches the macOS build of Baldur's Gate 3 and only builds on macOS." )
endif()

# Mach-O parsing and the instruction writes copy raw little-endian bytes in host order. With several architectures, the
# byte order is only set when they all share it, so an unknown or mixed one is rejected as well.
if( NOT CMAKE_CXX_BYTE_ORDER STREQUAL "LITTLE_ENDIAN" )
    message( FATAL_ERROR "This tool needs a little-endian target, got '${CMAKE_CXX_BYTE_ORDER}'. Build for a single architecture, e.g. arm64." )
endif()

if( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
    set( CMAKE_BUILD_TYPE Release CACHE STRING "Build type." FORCE )
endif()

file( GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp" )
cdrk_module( ${PROJECT_NAME}
             EXECUTABLE
             SOURCES ${SOURCE_FILES}
             STRICT_ARGS ENABLE_EXCEPTIONS )

target_precompile_headers( ${MODULE_TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.hpp" )
target_compile_definitions( ${MODULE_TARGET_NAME} PRIVATE BG3AE_VERSION="${PROJECT_VERSION}" )
