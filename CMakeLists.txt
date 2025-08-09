cmake_minimum_required(VERSION 3.16)
project(ETLPlusBackend)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    # Extract the actual library path for linking
    if(JSONCPP_LIBRARIES MATCHES "jsoncpp")
        set(JSONCPP_LIBRARIES "/opt/homebrew/Cellar/jsoncpp/1.9.6/lib/libjsoncpp.dylib")
        message(STATUS "Using jsoncpp library path: ${JSONCPP_LIBRARIES}")
    endif()
endif()

# Include directories
if(JSONCPP_INCLUDE_DIR)
    include_directories(${JSONCPP_INCLUDE_DIR})
endif()
if(JSONCPP_INCLUDE_DIRS)
    include_directories(${JSONCPP_INCLUDE_DIRS})
endif()
if(CURL_INCLUDE_DIRS)
    include_directories(${CURL_INCLUDE_DIRS})
endif()

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
    src/notification_service.cpp
)

# Create executable
add_executable(ETLPlusBackend ${SOURCES})

# Link libraries
target_link_libraries(ETLPlusBackend 
    ${Boost_LIBRARIES}
    Threads::Threads
    ${CURL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
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
    src/websocket_manager.cpp
    src/websocket_connection.cpp
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
    src/websocket_manager.cpp
    src/websocket_connection.cpp
    src/job_monitoring_models.cpp
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

# Log Streaming Test Executable
add_executable(test_log_streaming_simple scripts/test_log_streaming_simple.cpp 
    src/job_monitoring_models.cpp
    src/websocket_manager.cpp
    src/websocket_connection.cpp
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_log_streaming_simple PRIVATE include)
target_link_libraries(test_log_streaming_simple ${Boost_LIBRARIES} pthread)

# Log Streaming Integration Test Executable
add_executable(test_log_streaming_integration scripts/test_log_streaming_integration.cpp 
    src/job_monitoring_models.cpp
    src/websocket_manager.cpp
    src/websocket_connection.cpp
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_log_streaming_integration PRIVATE include)
target_link_libraries(test_log_streaming_integration ${Boost_LIBRARIES} pthread)

# Log Streaming Demo Executable
add_executable(test_log_streaming_demo scripts/test_log_streaming_demo.cpp 
    src/job_monitoring_models.cpp
    src/websocket_manager.cpp
    src/websocket_connection.cpp
    src/logger.cpp 
    src/config_manager.cpp
)

target_include_directories(test_log_streaming_demo PRIVATE include)
target_link_libraries(test_log_streaming_demo ${Boost_LIBRARIES} pthread)

# Enhanced REST Endpoints Test Executable
add_executable(test_enhanced_rest_endpoints_simple scripts/test_enhanced_rest_endpoints_simple.cpp 
    src/input_validator.cpp
    src/logger.cpp 
    src/config_manager.cpp
    src/websocket_manager.cpp
    src/websocket_connection.cpp
    src/job_monitoring_models.cpp
)

target_include_directories(test_enhanced_rest_endpoints_simple PRIVATE include)
target_link_libraries(test_enhanced_rest_endpoints_simple ${Boost_LIBRARIES} pthread)

# Configuration Management Test Executable (Requires GoogleTest - commented out until GoogleTest is available)
# add_executable(test_config_management scripts/test_config_management.cpp 
#     src/config_manager.cpp
#     src/logger.cpp 
# )

# target_include_directories(test_config_management PRIVATE include)
# target_link_libraries(test_config_management 
#     ${Boost_LIBRARIES} 
#     pthread
#     gtest 
#     gtest_main
#     gmock
# )

# Configuration Management Demo Test Executable
add_executable(test_config_management_demo scripts/test_config_management_demo.cpp 
    src/config_manager.cpp
    src/logger.cpp 
    src/websocket_manager.cpp
    src/websocket_connection.cpp
    src/job_monitoring_models.cpp
)

target_include_directories(test_config_management_demo PRIVATE include)
target_link_libraries(test_config_management_demo ${Boost_LIBRARIES} pthread)

# Simple Configuration Test Executable
add_executable(test_config_simple scripts/test_config_simple.cpp 
    src/config_manager.cpp
    src/logger.cpp 
    src/websocket_manager.cpp
    src/websocket_connection.cpp
    src/job_monitoring_models.cpp
)

target_include_directories(test_config_simple PRIVATE include)
target_link_libraries(test_config_simple ${Boost_LIBRARIES} pthread)

# Notification Service Test Executable (Requires GoogleTest - commented out)
# add_executable(test_notification_service scripts/test_notification_service.cpp 
#     src/notification_service.cpp
#     src/logger.cpp 
#     src/config_manager.cpp
# )

# target_include_directories(test_notification_service PRIVATE include)
# target_link_libraries(test_notification_service 
#     ${Boost_LIBRARIES} 
#     ${CURL_LIBRARIES}
#     ${JSONCPP_LIBRARIES}
#     pthread
#     gtest 
#     gtest_main
#     gmock
# )

# Notification Service Integration Demo (commented out due to missing WebSocketManager dependencies)
# add_executable(test_notification_service_integration scripts/test_notification_service_integration.cpp 
#     src/notification_service.cpp
#     src/logger.cpp 
#     src/config_manager.cpp
# )

# target_include_directories(test_notification_service_integration PRIVATE include)
# target_link_libraries(test_notification_service_integration 
#     ${Boost_LIBRARIES} 
#     ${CURL_LIBRARIES}
#     ${JSONCPP_LIBRARIES}
#     pthread
# )
