cmake_minimum_required(VERSION 3.16)
project(ETLPlusBackend)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Enable parallel compilation
if(MSVC)
    add_compile_options(/MP)
endif()

# Use Ninja generator if available (add to your build command: cmake -GNinja)
# Or enable parallel builds: make -j$(nproc) or cmake --build . --parallel

# Find required packages
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(Threads REQUIRED)

# Try to find libcurl
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    message(STATUS "libcurl not found via find_package, trying pkg-config")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CURL QUIET libcurl)
    endif()
    
    if(NOT CURL_FOUND)
        message(STATUS "libcurl not found, will build without webhook support")
        set(CURL_LIBRARIES "")
        set(CURL_INCLUDE_DIRS "")
    endif()
else()
    message(STATUS "Found libcurl: ${CURL_LIBRARIES}")
endif()

# Try to find jsoncpp
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(JSONCPP QUIET jsoncpp)
endif()

if(NOT JSONCPP_FOUND)
    message(STATUS "jsoncpp not found via pkg-config, trying manual search")
    
    # First, try to find jsoncpp installed via Homebrew
    find_path(JSONCPP_INCLUDE_DIR json/json.h 
        PATHS 
            /opt/homebrew/Cellar/jsoncpp/1.9.6/include
            /opt/homebrew/include
            /usr/local/include
            /usr/include/jsoncpp 
            /usr/include
        NO_DEFAULT_PATH
    )
    find_library(JSONCPP_LIBRARIES 
        NAMES jsoncpp
        PATHS 
            /opt/homebrew/Cellar/jsoncpp/1.9.6/lib
            /opt/homebrew/lib
            /usr/local/lib
            /usr/lib
        NO_DEFAULT_PATH
    )
    
    if(JSONCPP_INCLUDE_DIR AND JSONCPP_LIBRARIES)
        set(JSONCPP_FOUND TRUE)
        message(STATUS "Found jsoncpp manually: ${JSONCPP_LIBRARIES}")
        message(STATUS "jsoncpp include dir: ${JSONCPP_INCLUDE_DIR}")
    else()
        message(STATUS "jsoncpp not found, will build without JSON support")
        set(JSONCPP_LIBRARIES "")
        set(JSONCPP_INCLUDE_DIRS "")
    endif()
else()
    message(STATUS "Found jsoncpp via pkg-config: ${JSONCPP_LIBRARIES}")
    if(JSONCPP_LIBRARIES MATCHES "jsoncpp")
        set(JSONCPP_LIBRARIES "/opt/homebrew/Cellar/jsoncpp/1.9.6/lib/libjsoncpp.dylib")
        message(STATUS "Using jsoncpp library path: ${JSONCPP_LIBRARIES}")
    endif()
endif()

# Create a shared library for common sources to avoid recompilation
set(COMMON_SOURCES
    src/logger.cpp
    src/config_manager.cpp
    src/database_manager.cpp
    src/websocket_connection.cpp
    src/websocket_manager.cpp
    src/data_transformer.cpp
    src/auth_manager.cpp
    src/etl_job_manager.cpp
    src/input_validator.cpp
    src/exceptions.cpp
    src/exception_handler.cpp
    src/job_monitoring_models.cpp
    src/job_monitor_service.cpp
    src/notification_service.cpp
)

add_library(etl_common STATIC ${COMMON_SOURCES})

# Set include directories for the library
target_include_directories(etl_common PUBLIC include)
if(JSONCPP_INCLUDE_DIR)
    target_include_directories(etl_common PUBLIC ${JSONCPP_INCLUDE_DIR})
endif()
if(JSONCPP_INCLUDE_DIRS)
    target_include_directories(etl_common PUBLIC ${JSONCPP_INCLUDE_DIRS})
endif()
if(CURL_INCLUDE_DIRS)
    target_include_directories(etl_common PUBLIC ${CURL_INCLUDE_DIRS})
endif()

# Link libraries to the common library
target_link_libraries(etl_common PUBLIC
    ${Boost_LIBRARIES}
    Threads::Threads
    ${CURL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
)

# Main executable - now much simpler
add_executable(ETLPlusBackend 
    src/main.cpp
    src/http_server.cpp
    src/request_handler.cpp
)

target_include_directories(ETLPlusBackend PRIVATE include)
target_link_libraries(ETLPlusBackend etl_common)

# Set output directory
set_target_properties(ETLPlusBackend PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Create necessary directories
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin/logs)

# Copy config file to build directory - this will copy every time if the source changes
configure_file(${CMAKE_SOURCE_DIR}/config/config.json ${CMAKE_BINARY_DIR}/bin/config.json COPYONLY)

# Create a custom target to copy all config files
add_custom_target(copy_configs ALL
    COMMENT "Copying configuration files to build directory"
)

# Add a dependency so the executable depends on config files being copied
add_dependencies(ETLPlusBackend copy_configs)

# Add commands to copy any additional config files if they exist
file(GLOB CONFIG_FILES "${CMAKE_SOURCE_DIR}/config/*")
foreach(CONFIG_FILE ${CONFIG_FILES})
    get_filename_component(CONFIG_FILENAME ${CONFIG_FILE} NAME)
    add_custom_command(TARGET copy_configs PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CONFIG_FILE} ${CMAKE_BINARY_DIR}/bin/${CONFIG_FILENAME}
        COMMENT "Copying ${CONFIG_FILENAME}"
    )
endforeach()

# Macro to create test executables more efficiently
macro(create_test_executable target_name script_file)
    add_executable(${target_name} ${script_file})
    target_include_directories(${target_name} PRIVATE include)
    target_link_libraries(${target_name} etl_common)
endmacro()

# Create all test executables using the shared library
create_test_executable(test_websocket_basic scripts/test_websocket_basic.cpp)
create_test_executable(test_websocket_integration scripts/test_websocket_integration.cpp)
create_test_executable(test_websocket_manager_enhanced scripts/test_websocket_manager_enhanced.cpp)
create_test_executable(test_websocket_filtering_demo scripts/test_websocket_filtering_demo.cpp)
create_test_executable(test_job_monitoring_models scripts/test_job_monitoring_models.cpp)
create_test_executable(test_etl_job_manager_monitoring scripts/test_etl_job_manager_monitoring.cpp)
create_test_executable(test_job_monitor_service scripts/test_job_monitor_service.cpp)
create_test_executable(test_job_monitor_service_simple scripts/test_job_monitor_service_simple.cpp)
create_test_executable(test_log_streaming_simple scripts/test_log_streaming_simple.cpp)
create_test_executable(test_log_streaming_integration scripts/test_log_streaming_integration.cpp)
create_test_executable(test_log_streaming_demo scripts/test_log_streaming_demo.cpp)
create_test_executable(test_enhanced_rest_endpoints_simple scripts/test_enhanced_rest_endpoints_simple.cpp)
create_test_executable(test_config_management_demo scripts/test_config_management_demo.cpp)
create_test_executable(test_config_simple scripts/test_config_simple.cpp)

# Optional: Enable precompiled headers for common includes (C++20 feature)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(etl_common PRIVATE
        <iostream>
        <string>
        <vector>
        <memory>
        <functional>
        <boost/beast.hpp>
        <boost/asio.hpp>
    )
endif()

# Mac-specific optimizations
if(APPLE)
    # Use all CPU cores for compilation
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N})
        message(STATUS "Using ${N} parallel build jobs")
    endif()
    
    # Enable ccache if available
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    endif()
    
    # Mac-specific compiler flags for better performance
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
    
    # Use libc++ (should be default on modern macOS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif()
