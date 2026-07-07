# =============================================================================
# Nyx - A headless decompilation engine
# Copyright (C) 2024-2026 Chapzoo
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(nyx
    VERSION 0.2.0
    DESCRIPTION "Nyx - a headless decompilation engine"
    HOMEPAGE_URL "https://github.com/Chapzoo/nyx"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# C++20, hard requirements
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Global compile options - warnings are aggressive but not -Werror by default
# (sanitizers / CI enable -Werror)
# ---------------------------------------------------------------------------
add_library(nyx_compiler_flags INTERFACE)
add_library(nyx::compiler_flags ALIAS nyx_compiler_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nyx_compiler_flags INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Woverloaded-virtual -Wnull-dereference
        -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough
        -Wno-unused-parameter
    )
    # Force coloured diagnostics when in a terminal/pipeline that supports it
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(nyx_compiler_flags INTERFACE -fdiagnostics-color=always)
    else()
        target_compile_options(nyx_compiler_flags INTERFACE -fcolor-diagnostics)
    endif()
endif()

# ---------------------------------------------------------------------------
# Output dirs - keep build tree clean
# ---------------------------------------------------------------------------
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Sanitizer toggle (default OFF for Release, ON for CI verification)
# ---------------------------------------------------------------------------
option(NYX_ENABLE_SANITIZERS "Enable ASan+UBSan (debug/CI only)" OFF)
option(NYX_ENABLE_COVERAGE    "Enable code coverage instrumentation" OFF)
option(NYX_BUILD_TESTS        "Build the test suite (doctest)" ON)
option(NYX_WERROR             "Treat warnings as errors" OFF)
option(NYX_BUILD_PYTHON       "Build Python bindings via pybind11 (optional)" OFF)

if(NYX_WERROR)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(nyx_compiler_flags INTERFACE -Werror)
    endif()
endif()

if(NYX_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(nyx_compiler_flags INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-common
        )
        target_link_options(nyx_compiler_flags INTERFACE
            -fsanitize=address,undefined
        )
        message(STATUS "Nyx: ASan + UBSan enabled")
    else()
        message(WARNING "Nyx: sanitizers requested but compiler does not support them")
    endif()
endif()

if(NYX_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(nyx_compiler_flags INTERFACE --coverage -O0 -g)
        target_link_options(nyx_compiler_flags INTERFACE --coverage)
    endif()
endif()

# ---------------------------------------------------------------------------
# FetchContent: Capstone (disassembly) + doctest (tests)
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Capstone v5.0.3 - lightweight multi-arch disassembler.
# Build only what Nyx uses to keep memory footprint low on constrained hosts.
set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
set(CAPSTONE_ARM64_SUPPORT ON CACHE BOOL "" FORCE)
set(CAPSTONE_ARM_SUPPORT   ON CACHE BOOL "" FORCE)
set(CAPSTONE_X86_SUPPORT   ON CACHE BOOL "" FORCE)
set(CAPSTONE_PPC_SUPPORT   ON CACHE BOOL "" FORCE)
set(CAPSTONE_MIPS_SUPPORT  ON CACHE BOOL "" FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "" FORCE)

# When building Python bindings, Capstone must also be PIC.
if(NYX_BUILD_PYTHON)
    set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    capstone
    GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
    GIT_TAG        5.0.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(capstone)

# When building Python bindings, Capstone must also be PIC.
if(NYX_BUILD_PYTHON)
    set_target_properties(capstone PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(NYX_BUILD_TESTS)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.12
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(doctest)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# Nyx library target
# ---------------------------------------------------------------------------
add_library(nyx STATIC
    src/core/logger.cpp
    src/core/bytes.cpp
    src/core/arch.cpp
    src/core/error.cpp
    src/core/version.cpp
    src/parsers/binary_parser.cpp
    src/parsers/elf_parser.cpp
    src/parsers/pe_parser.cpp
    src/parsers/macho_parser.cpp
    src/parsers/disassembler.cpp
    src/parsers/dwarf_parser.cpp
    src/parsers/wasm_parser.cpp
    src/lifter/ir.cpp
    src/lifter/cfg.cpp
    src/lifter/cfg_analysis.cpp
    src/lifter/ssa_builder.cpp
    src/lifter/ssa_optimizer.cpp
    src/lifter/region_builder.cpp
    src/lifter/instruction_lifter.cpp
    src/decompiler/decompiler.cpp
    src/decompiler/function_detector.cpp
    src/decompiler/type_inferer.cpp
    src/decompiler/jump_table_detector.cpp
    src/decompiler/calling_convention.cpp
    src/decompiler/pseudo_c.cpp
    src/output/json_writer.cpp
    src/output/text_writer.cpp
    src/output/pseudo_c_writer.cpp
    src/output/dot_writer.cpp
    src/output/annotated_writer.cpp
)
add_library(nyx::nyx ALIAS nyx)

# When building Python bindings, the nyx library must be PIC (shared module).
if(NYX_BUILD_PYTHON)
    set_target_properties(nyx PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_include_directories(nyx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nyx PUBLIC
    capstone
    nyx::compiler_flags
)

target_compile_features(nyx PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# nyx CLI executable
# ---------------------------------------------------------------------------
add_executable(nyx_cli src/main.cpp)
set_target_properties(nyx_cli PROPERTIES OUTPUT_NAME nyx)
target_link_libraries(nyx_cli PRIVATE nyx::nyx)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NYX_BUILD_TESTS)
    # Single doctest main entry - other test TUs must NOT define
    # DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (causes multiple-definition errors).
    add_executable(nyx_unit_tests
        tests/main.cpp
        tests/unit/test_bytes.cpp
        tests/unit/test_arch.cpp
        tests/unit/test_logger.cpp
        tests/unit/test_ir.cpp
        tests/unit/test_cfg.cpp
        tests/unit/test_elf_parser.cpp
        tests/unit/test_pe_parser.cpp
        tests/unit/test_macho_parser.cpp
        tests/unit/test_disassembler.cpp
        tests/unit/test_instruction_lifter.cpp
        tests/unit/test_arm64_lifter.cpp
        tests/unit/test_x86_lifter.cpp
        tests/unit/test_arm32_lifter.cpp
        tests/unit/test_ppc_lifter.cpp
        tests/unit/test_mips_lifter.cpp
        tests/unit/test_decompiler.cpp
        tests/unit/test_type_inferer.cpp
        tests/unit/test_function_detector_v004.cpp
        tests/unit/test_dot_writer.cpp
        tests/unit/test_cfg_analysis.cpp
        tests/unit/test_jump_table_detector.cpp
        tests/unit/test_dwarf_parser.cpp
        tests/unit/test_annotated_writer.cpp
        tests/unit/test_ssa_builder.cpp
        tests/unit/test_calling_convention.cpp
        tests/unit/test_ssa_optimizer.cpp
        tests/unit/test_region_builder.cpp
        tests/unit/test_wasm_parser.cpp
        tests/unit/test_json_writer.cpp
        tests/unit/test_text_writer.cpp
        tests/unit/test_pseudo_c_writer.cpp
    )
    target_link_libraries(nyx_unit_tests PRIVATE nyx::nyx doctest nyx::compiler_flags)
    target_compile_definitions(nyx_unit_tests PRIVATE
        NYX_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )

    add_executable(nyx_integration_tests
        tests/main.cpp
        tests/integration/test_pipeline_elf.cpp
        tests/integration/test_pipeline_pe.cpp
        tests/integration/test_pipeline_macho.cpp
        tests/integration/test_pipeline_arm64.cpp
        tests/integration/test_v002_features.cpp
        tests/integration/test_v003_features.cpp
        tests/integration/test_v004_features.cpp
        tests/integration/test_v005_features.cpp
        tests/integration/test_v006_features.cpp
        tests/integration/test_cli.cpp
    )
    target_link_libraries(nyx_integration_tests PRIVATE nyx::nyx doctest nyx::compiler_flags)
    target_compile_definitions(nyx_integration_tests PRIVATE
        NYX_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        NYX_BINARY_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )

    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake OPTIONAL RESULT_VARIABLE _doctest_cmake)
    # Fallback: just register ctest targets if doctest.cmake is unavailable
    add_test(NAME nyx_unit_tests          COMMAND nyx_unit_tests)
    add_test(NAME nyx_integration_tests   COMMAND nyx_integration_tests)
endif()

# ---------------------------------------------------------------------------
# Install rules (packaging-friendly)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS nyx nyx_cli
    EXPORT nyxTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/nyx DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# ---------------------------------------------------------------------------
# Version metadata used by nyx --version
# ---------------------------------------------------------------------------
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/nyx/version.hpp.in
    ${CMAKE_CURRENT_SOURCE_DIR}/include/nyx/version.hpp
    @ONLY
)

# ---------------------------------------------------------------------------
# Optional Python bindings (pybind11 via FetchContent)
# ---------------------------------------------------------------------------
if(NYX_BUILD_PYTHON)
    # Find Python first.
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

    # Fetch pybind11.
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.6
        GIT_SHALLOW    TRUE
    )
    # Suppress pybind11's own warnings about CMake versions.
    set(PYBIND11_FINDPYTHON OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(pybind11)

    pybind11_add_module(nyx_python bindings/python/nyx_python.cpp)
    target_link_libraries(nyx_python PRIVATE nyx::nyx)
    target_compile_features(nyx_python PRIVATE cxx_std_20)

    # Install the module to the Python site-packages.
    install(TARGETS nyx_python LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/nyx)
endif()
