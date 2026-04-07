cmake_minimum_required(VERSION 3.20)
project(GuPT VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Platform checks
if(NOT WIN32)
    message(FATAL_ERROR "This project is Windows-only")
endif()

# Configuration options
option(BUILD_TESTS "Build test programs" ON)
option(USE_NVENC "Use NVIDIA NVENC for encoding" ON)
option(USE_NVDEC "Use NVIDIA NVDEC for decoding" OFF)
option(USE_FFMPEG "Use FFmpeg for encoding/decoding fallback" ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/common)
include_directories(${CMAKE_SOURCE_DIR}/third_party/nvenc/include)

# Common library
add_library(gupt_common STATIC
    common/protocol.h
    common/packet.h
    common/utils.h
    common/utils.cpp
    common/logger.h
    common/logger.cpp
)

target_compile_definitions(gupt_common PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

# Host executable
add_executable(gupt_host
    host/main.cpp
    host/capture.h
    host/capture.cpp
    host/encoder.h
    host/encoder.cpp
    host/network_server.h
    host/network_server.cpp
    # Input handler (future phase)
    # host/input_handler.h
    # host/input_handler.cpp
)

target_link_libraries(gupt_host PRIVATE
    gupt_common
    d3d11.lib
    dxgi.lib
    ws2_32.lib
)

if(USE_NVENC)
    target_compile_definitions(gupt_host PRIVATE USE_NVENC)
endif()

# Client executable
add_executable(gupt_client
    client/main.cpp
    client/decoder.h
    client/decoder.cpp
    client/renderer.h
    client/renderer.cpp
    client/network_client.h
    client/network_client.cpp
    client/input_sender.h
    client/input_sender.cpp
)

target_link_libraries(gupt_client PRIVATE
    gupt_common
    d3d11.lib
    dxgi.lib
    ws2_32.lib
)

# Tests
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Installation
install(TARGETS gupt_host gupt_client
    RUNTIME DESTINATION bin
)

# Print configuration
message(STATUS "=== GuPT Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "USE_NVENC: ${USE_NVENC}")
message(STATUS "USE_NVDEC: ${USE_NVDEC}")
message(STATUS "USE_FFMPEG: ${USE_FFMPEG}")
message(STATUS "BUILD_TESTS: ${BUILD_TESTS}")
