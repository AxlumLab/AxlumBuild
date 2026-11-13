# @Author: khfahqp khfahqp@gmail.com
# @LastEditors: khfahqp khfahqp@gmail.com
# Copyright (c) 2026 by AxlumLab, All Rights Reserved.

cmake_minimum_required(VERSION 3.25)

option(AXLUM_STATIC_RELEASE "Statically link release dependencies and supported platform runtimes" OFF)
if(AXLUM_STATIC_RELEASE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Build configuration
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build")
endif()

# Select CRT before project() initializes the compiler, matching the vcpkg triplet.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND VCPKG_TARGET_TRIPLET MATCHES "-static$")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Stable English diagnostics keep Ninja/MSVC dependency parsing independent of locale.
if(CMAKE_HOST_WIN32)
    set(ENV{VSLANG} "1033")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

project(AxlumBuild VERSION 0.1.0 LANGUAGES CXX)

option(AXLUM_BUILD_TESTS "Build AxlumBuild tests" ${PROJECT_IS_TOP_LEVEL})
option(AXLUM_USE_INSTALLED_RHODIUM "Use an installed Rhodium::Core instead of the submodule" OFF)

# Shared target settings
function(axlumConfigureTarget target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Dependencies
# Keep imported dependencies in the root directory scope for all subprojects.
find_package(OpenSSL REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

if(AXLUM_USE_INSTALLED_RHODIUM)
    find_package(Rhodium CONFIG REQUIRED)
else()
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/rhodium/CMakeLists.txt")
        message(FATAL_ERROR "Rhodium submodule missing. Run: git submodule update --init --recursive")
    endif()
    set(RHODIUM_BUILD_CLI OFF CACHE BOOL "Build Rhodium CLI" FORCE)
    set(RHODIUM_BUILD_TESTS OFF CACHE BOOL "Build Rhodium tests" FORCE)
    set(RHODIUM_INSTALL OFF CACHE BOOL "Install Rhodium" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/rhodium" "${PROJECT_BINARY_DIR}/third_party/rhodium" EXCLUDE_FROM_ALL)
endif()

# Core library
add_library(axlum-build-core STATIC
    src/backend/Ide.cpp
    src/backend/Ninja.cpp
    src/core/BuildOperations.cpp
    src/core/Diagnostics.cpp
    src/core/Process.cpp
    src/graph/Graph.cpp
    src/graph/Hooks.cpp
    src/graph/Modules.cpp
    src/graph/NativeOptions.cpp
    src/graph/NativePaths.cpp
    src/graph/Pch.cpp
    src/graph/Resources.cpp
    src/graph/Runtime.cpp
    src/managed/DotNet.cpp
    src/managed/NativeImage.cpp
    src/managed/NativeProject.cpp
    src/managed/Project.cpp
    src/manifest/Composition.cpp
    src/manifest/LuaManifest.cpp
    src/manifest/Manifest.cpp
    src/model/Model.cpp
    src/packages/Rhodium.cpp
    src/toolchain/Toolchain.cpp
)
add_library(AxlumBuild::Core ALIAS axlum-build-core)
if(MSVC)
    # sol2's Lua bindings exceed the regular COFF section limit in Debug builds.
    set_property(SOURCE src/manifest/LuaManifest.cpp APPEND PROPERTY COMPILE_OPTIONS /bigobj)
endif()
axlumConfigureTarget(axlum-build-core)
target_include_directories(axlum-build-core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
target_link_libraries(axlum-build-core PRIVATE Rhodium::Core OpenSSL::Crypto)


# Command-line application
add_executable(axlum-build src/cli/Application.cpp src/cli/Main.cpp)
axlumConfigureTarget(axlum-build)
target_link_libraries(axlum-build PRIVATE AxlumBuild::Core CLI11::CLI11)
if(AXLUM_STATIC_RELEASE)
    get_target_property(rhodiumLibraryType Rhodium::Core TYPE)
    if(NOT rhodiumLibraryType STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "AXLUM_STATIC_RELEASE requires a static Rhodium::Core")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(axlum-build PRIVATE -static)
    endif()
endif()

# Validation
function(axlumAddPythonTest name script)
    cmake_parse_arguments(PARSE_ARGV 2 test "NO_EXAMPLES" "TIMEOUT;SKIP_REGEX;REQUIRES" "")
    if(NOT Python3_Interpreter_FOUND)
        return()
    endif()
    if(test_REQUIRES AND NOT EXISTS "${PROJECT_SOURCE_DIR}/${test_REQUIRES}")
        message(STATUS "Skipping ${name}: optional fixture ${test_REQUIRES} is absent")
        return()
    endif()

    set(arguments "$<TARGET_FILE:axlum-build>")
    if(NOT test_NO_EXAMPLES)
        list(APPEND arguments "${PROJECT_SOURCE_DIR}/examples")
    endif()
    add_test(NAME "${name}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/${script}" ${arguments})
    if(test_TIMEOUT)
        set_tests_properties("${name}" PROPERTIES TIMEOUT "${test_TIMEOUT}")
    endif()
    if(test_SKIP_REGEX)
        set_tests_properties("${name}" PROPERTIES SKIP_REGULAR_EXPRESSION "${test_SKIP_REGEX}")
    endif()
endfunction()

if(AXLUM_BUILD_TESTS)
    enable_testing()
    add_executable(axlum-unit-tests tests/Unit.cpp)
    axlumConfigureTarget(axlum-unit-tests)
    target_link_libraries(axlum-unit-tests PRIVATE AxlumBuild::Core)
    add_test(NAME unit COMMAND axlum-unit-tests)

    add_executable(axlum-native-plan-tests tests/NativePlan.cpp)
    axlumConfigureTarget(axlum-native-plan-tests)
    target_link_libraries(axlum-native-plan-tests PRIVATE AxlumBuild::Core)
    add_test(NAME native-plan COMMAND axlum-native-plan-tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    axlumAddPythonTest(integration Integration.py TIMEOUT 600)
    axlumAddPythonTest(cli-dsl CliDsl.py NO_EXAMPLES TIMEOUT 600)
    axlumAddPythonTest(packages Packages.py TIMEOUT 600)
    axlumAddPythonTest(native-features NativeFeatures.py TIMEOUT 600)
    axlumAddPythonTest(modules-reconfigure ModulesReconfigure.py TIMEOUT 360)
    axlumAddPythonTest(ide-workspace IdeWorkspace.py TIMEOUT 600)
    axlumAddPythonTest(cross-matrix CrossMatrix.py TIMEOUT 600 SKIP_REGEX "SKIP: real Android")
    axlumAddPythonTest(diagnostics Diagnostics.py NO_EXAMPLES TIMEOUT 600)
    axlumAddPythonTest(managed-sdk ManagedSdk.py TIMEOUT 600)
    axlumAddPythonTest(dotnet-publish DotNetPublish.py TIMEOUT 900)
    axlumAddPythonTest(parity-smoke ParitySmoke.py TIMEOUT 300)
    axlumAddPythonTest(final-gaps FinalGaps.py TIMEOUT 240 SKIP_REGEX "SKIP: MSVC" REQUIRES examples/final-gaps/axlum.build.lua)
    axlumAddPythonTest(target-selection TargetSelection.py TIMEOUT 240 SKIP_REGEX "SKIP: native vcxproj")
    axlumAddPythonTest(command-lifecycle CommandLifecycle.py TIMEOUT 300)
endif()

# Installation
include(GNUInstallDirs)
install(TARGETS axlum-build
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Runtime
)
