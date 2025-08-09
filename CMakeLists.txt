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
    src/websocket_connection.cpp
    src/websocket_manager.cpp
    src/data_transformer.cpp
    src/auth_manager.cpp
    src/etl_job_manager.cpp
    src/request_handler.cpp
    src/input_validator.cpp
    src/exceptions.cpp
    src/exception_handler.cpp
    src/job_monitoring_models.cpp
    src/job_monitor_service.cpp
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

# WebSocket Basic Test Executable
add_executable(test_websocket_basic scripts/test_websocket_basic.cpp 
    src/websocket_connection.cpp
    src/websocket_manager.cpp
    src/job_monitoring_models.cpp
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_websocket_basic PRIVATE include)
target_link_libraries(test_websocket_basic ${Boost_LIBRARIES} pthread)

# WebSocket Integration Test Executable
add_executable(test_websocket_integration scripts/test_websocket_integration.cpp 
    src/websocket_connection.cpp
    src/websocket_manager.cpp
    src/job_monitoring_models.cpp
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_websocket_integration PRIVATE include)
target_link_libraries(test_websocket_integration ${Boost_LIBRARIES} pthread)

# Enhanced WebSocket Manager Test Executable
add_executable(test_websocket_manager_enhanced scripts/test_websocket_manager_enhanced.cpp 
    src/websocket_connection.cpp
    src/websocket_manager.cpp
    src/job_monitoring_models.cpp
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_websocket_manager_enhanced PRIVATE include)
target_link_libraries(test_websocket_manager_enhanced ${Boost_LIBRARIES} pthread)

# WebSocket Filtering Demo Executable
add_executable(test_websocket_filtering_demo scripts/test_websocket_filtering_demo.cpp 
    src/websocket_connection.cpp
    src/websocket_manager.cpp
    src/job_monitoring_models.cpp
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_websocket_filtering_demo PRIVATE include)
target_link_libraries(test_websocket_filtering_demo ${Boost_LIBRARIES} pthread)

# Job Monitoring Models Test Executable
add_executable(test_job_monitoring_models scripts/test_job_monitoring_models.cpp 
    src/job_monitoring_models.cpp
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_job_monitoring_models PRIVATE include)
target_link_libraries(test_job_monitoring_models ${Boost_LIBRARIES} pthread)

# ETL Job Manager Monitoring Test Executable
add_executable(test_etl_job_manager_monitoring scripts/test_etl_job_manager_monitoring.cpp 
    src/etl_job_manager.cpp
    src/database_manager.cpp
    src/data_transformer.cpp
    src/logger.cpp 
    src/config_manager.cpp
    src/exceptions.cpp
    src/exception_handler.cpp
)

target_include_directories(test_etl_job_manager_monitoring PRIVATE include)
target_link_libraries(test_etl_job_manager_monitoring ${Boost_LIBRARIES} pthread)

# Job Monitor Service Test Executable
add_executable(test_job_monitor_service scripts/test_job_monitor_service.cpp 
    src/job_monitor_service.cpp
    src/job_monitoring_models.cpp
    src/etl_job_manager.cpp
    src/websocket_manager.cpp
    src/websocket_connection.cpp
    src/database_manager.cpp
    src/data_transformer.cpp
    src/logger.cpp 
    src/config_manager.cpp
    src/exceptions.cpp
    src/exception_handler.cpp
)

target_include_directories(test_job_monitor_service PRIVATE include)
target_link_libraries(test_job_monitor_service ${Boost_LIBRARIES} pthread)

# Simple Job Monitor Service Test Executable
add_executable(test_job_monitor_service_simple scripts/test_job_monitor_service_simple.cpp 
    src/job_monitor_service.cpp
    src/job_monitoring_models.cpp
    src/etl_job_manager.cpp
    src/websocket_manager.cpp
    src/websocket_connection.cpp
    src/database_manager.cpp
    src/data_transformer.cpp
    src/logger.cpp 
    src/config_manager.cpp
    src/exceptions.cpp
    src/exception_handler.cpp
)

target_include_directories(test_job_monitor_service_simple PRIVATE include)
target_link_libraries(test_job_monitor_service_simple ${Boost_LIBRARIES} pthread)
