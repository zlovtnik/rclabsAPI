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
    src/logger.cpp
    src/config_manager.cpp
    src/database_manager.cpp
    src/http_server.cpp
    src/data_transformer.cpp
    src/auth_manager.cpp
    src/etl_job_manager.cpp
    src/request_handler.cpp
    src/input_validator.cpp
    src/exceptions.cpp
    src/exception_handler.cpp
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

# Enhanced Logging Test Executable
add_executable(test_enhanced_logging test_enhanced_logging.cpp 
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_enhanced_logging PRIVATE include)
target_link_libraries(test_enhanced_logging ${Boost_LIBRARIES} pthread)

# Copy config files for the test
add_dependencies(test_enhanced_logging copy_configs)

# JSON Logging Test Executable
add_executable(test_json_logging test_json_logging.cpp 
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_json_logging PRIVATE include)
target_link_libraries(test_json_logging ${Boost_LIBRARIES} pthread)

# Async Logging Test Executable
add_executable(test_async_logging test_async_logging.cpp 
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_async_logging PRIVATE include)
target_link_libraries(test_async_logging ${Boost_LIBRARIES} pthread)
