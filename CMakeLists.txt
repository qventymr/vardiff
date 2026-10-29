cmake_minimum_required(VERSION 3.20)

project(vardiff VERSION 0.2.0
    DESCRIPTION "Header-only C++20 field change tracking and snapshots"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(vardiff INTERFACE)
add_library(vardiff::vardiff ALIAS vardiff)
target_compile_features(vardiff INTERFACE cxx_std_20)
target_include_directories(vardiff INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# Do not add examples or tests to a consuming project's default build.
set(VARDIFF_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(VARDIFF_TOP_LEVEL ON)
endif()
option(VARDIFF_BUILD_TESTS "Build vardiff tests" ${VARDIFF_TOP_LEVEL})
option(VARDIFF_BUILD_EXAMPLES "Build vardiff examples" ${VARDIFF_TOP_LEVEL})
option(VARDIFF_BUILD_BENCHMARKS "Build the standalone benchmark" OFF)
option(VARDIFF_WARNINGS_AS_ERRORS "Treat vardiff's own compiler warnings as errors" OFF)

function(vardiff_configure_executable target)
    target_link_libraries(${target} PRIVATE vardiff::vardiff)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(VARDIFF_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(VARDIFF_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

if(VARDIFF_BUILD_EXAMPLES)
    add_executable(vardiff_player examples/player.cpp)
    vardiff_configure_executable(vardiff_player)
    foreach(example IN ITEMS events nested)
        add_executable(vardiff_${example} examples/${example}.cpp)
        vardiff_configure_executable(vardiff_${example})
    endforeach()
endif()

if(VARDIFF_BUILD_BENCHMARKS)
    add_executable(vardiff_benchmark benchmarks/vardiff_benchmark.cpp)
    vardiff_configure_executable(vardiff_benchmark)
endif()

if(VARDIFF_BUILD_TESTS)
    enable_testing()
    add_executable(vardiff_tests tests/vardiff_tests.cpp tests/header_smoke.cpp)
    vardiff_configure_executable(vardiff_tests)
    add_test(NAME vardiff_tests COMMAND vardiff_tests)

    add_executable(vardiff_extended_tests tests/extended_tests.cpp)
    vardiff_configure_executable(vardiff_extended_tests)
    add_test(NAME vardiff_extended_tests COMMAND vardiff_extended_tests)

    if(VARDIFF_BUILD_EXAMPLES)
        add_test(NAME vardiff_player COMMAND vardiff_player)
        add_test(NAME vardiff_events COMMAND vardiff_events)
        add_test(NAME vardiff_nested COMMAND vardiff_nested)
    endif()
endif()

set(VARDIFF_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/vardiff")
configure_package_config_file(cmake/vardiffConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/vardiffConfig.cmake"
    INSTALL_DESTINATION "${VARDIFF_INSTALL_CMAKEDIR}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vardiffConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
    ARCH_INDEPENDENT)

install(TARGETS vardiff EXPORT vardiffTargets)
install(DIRECTORY include/vardiff DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT vardiffTargets NAMESPACE vardiff::
    DESTINATION "${VARDIFF_INSTALL_CMAKEDIR}")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vardiffConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vardiffConfigVersion.cmake"
    DESTINATION "${VARDIFF_INSTALL_CMAKEDIR}")
install(FILES LICENSE NOTICE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/vardiff")
