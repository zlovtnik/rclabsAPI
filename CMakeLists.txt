cmake_minimum_required(VERSION 3.16)
project(ETLPlusBackend)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(Threads REQUIRED)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/config_manager.cpp
    src/database_manager.cpp
    src/http_server.cpp
    src/data_transformer.cpp
    src/auth_manager.cpp
    src/etl_job_manager.cpp
    src/request_handler.cpp
)

# Create executable
add_executable(ETLPlusBackend ${SOURCES})

# Link libraries
target_link_libraries(ETLPlusBackend 
    ${Boost_LIBRARIES}
    Threads::Threads
)

# Set output directory
set_target_properties(ETLPlusBackend PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy config file to build directory
configure_file(${CMAKE_SOURCE_DIR}/config/config.json ${CMAKE_BINARY_DIR}/bin/config.json COPYONLY)
