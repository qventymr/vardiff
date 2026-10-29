cmake_minimum_required(VERSION 3.20)
project(vardiff_consumer LANGUAGES CXX)

find_package(vardiff 0.2 CONFIG REQUIRED)
add_executable(vardiff_consumer main.cpp)
target_link_libraries(vardiff_consumer PRIVATE vardiff::vardiff)
set_target_properties(vardiff_consumer PROPERTIES CXX_EXTENSIONS OFF)

enable_testing()
add_test(NAME vardiff_consumer COMMAND vardiff_consumer)
