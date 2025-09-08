#!/bin/bash

# ETL Plus Compatibility Layer Installer
# Installs compatibility shims for deprecated APIs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Global variables
TARGET_DIR="$PROJECT_ROOT"
DRY_RUN=false

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --target-dir)
                TARGET_DIR="$2"
                shift 2
                ;;
            *)
                log_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done
}

# Create compatibility header files
create_compatibility_headers() {
    log_info "Creating compatibility header files..."

    local compat_dir="$TARGET_DIR/include/compatibility"
    mkdir -p "$compat_dir"

    # Create old exception compatibility header
    cat > "$compat_dir/exceptions.hpp" << 'EOF'
#ifndef ETL_PLUS_COMPATIBILITY_EXCEPTIONS_HPP
#define ETL_PLUS_COMPATIBILITY_EXCEPTIONS_HPP

// Compatibility layer for old exception system
// This file provides backward compatibility for code still using old exception types

#include "etl_exceptions.hpp"
#include <string>

// Deprecated exception types - use ETLException instead
class DatabaseException : public ETLException {
public:
    explicit DatabaseException(const std::string& message, const std::string& correlationId = "unknown")
        : ETLException("Database error: " + message, correlationId, message) {}
};

class NetworkException : public ETLException {
public:
    explicit NetworkException(const std::string& message, const std::string& correlationId = "unknown")
        : ETLException("Network error: " + message, correlationId, message) {}
};

class ValidationException : public ETLException {
public:
    explicit ValidationException(const std::string& message, const std::string& correlationId = "unknown")
        : ETLException("Validation error: " + message, correlationId, message) {}
};

// Compatibility macros for old exception usage patterns
#define THROW_DB_EXCEPTION(msg) throw DatabaseException(msg, "legacy")
#define THROW_NETWORK_EXCEPTION(msg) throw NetworkException(msg, "legacy")
#define THROW_VALIDATION_EXCEPTION(msg) throw ValidationException(msg, "legacy")

#endif // ETL_PLUS_COMPATIBILITY_EXCEPTIONS_HPP
EOF

    # Create old logging compatibility header
    cat > "$compat_dir/logging.hpp" << 'EOF'
#ifndef ETL_PLUS_COMPATIBILITY_LOGGING_HPP
#define ETL_PLUS_COMPATIBILITY_LOGGING_HPP

// Compatibility layer for old logging system
// This file provides backward compatibility for code still using old logging macros

#include "component_logger.hpp"
#include <string>

// Global logger instance for compatibility
inline std::shared_ptr<ComponentLogger<struct LegacyLogger>> getLegacyLogger() {
    static std::shared_ptr<ComponentLogger<struct LegacyLogger>> logger =
        ComponentLogger<struct LegacyLogger>::create();
    return logger;
}

// Compatibility macros for old logging patterns
#define LOG_INFO(msg) getLegacyLogger()->info(msg, "legacy")
#define LOG_ERROR(msg) getLegacyLogger()->error(msg, "legacy")
#define LOG_DEBUG(msg) getLegacyLogger()->debug(msg, "legacy")
#define LOG_WARN(msg) getLegacyLogger()->warn(msg, "legacy")

// Compatibility class for old Logger interface
class Logger {
private:
    std::shared_ptr<ComponentLogger<Logger>> logger_;

public:
    Logger() : logger_(ComponentLogger<Logger>::create()) {}

    void info(const std::string& message) {
        logger_->info(message, "legacy");
    }

    void error(const std::string& message) {
        logger_->error(message, "legacy");
    }

    void debug(const std::string& message) {
        logger_->debug(message, "legacy");
    }

    void warn(const std::string& message) {
        logger_->warn(message, "legacy");
    }
};

#endif // ETL_PLUS_COMPATIBILITY_LOGGING_HPP
EOF

    # Create old configuration compatibility header
    cat > "$compat_dir/config.hpp" << 'EOF'
#ifndef ETL_PLUS_COMPATIBILITY_CONFIG_HPP
#define ETL_PLUS_COMPATIBILITY_CONFIG_HPP

// Compatibility layer for old configuration system
// This file provides backward compatibility for code still using old config access patterns

#include "config_manager.hpp"
#include <string>

// Compatibility class for old Config interface
class Config {
private:
    std::shared_ptr<ConfigManager> config_;

public:
    Config() : config_(std::make_shared<ConfigManager>()) {}

    std::string getString(const std::string& key, const std::string& defaultValue = "") {
        try {
            return config_->get<std::string>(key, defaultValue);
        } catch (const ConfigurationException&) {
            return defaultValue;
        }
    }

    int getInt(const std::string& key, int defaultValue = 0) {
        try {
            return config_->get<int>(key, defaultValue);
        } catch (const ConfigurationException&) {
            return defaultValue;
        }
    }

    bool getBool(const std::string& key, bool defaultValue = false) {
        try {
            return config_->get<bool>(key, defaultValue);
        } catch (const ConfigurationException&) {
            return defaultValue;
        }
    }

    // Template-based access (new pattern)
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T{}) {
        try {
            return config_->get<T>(key, defaultValue);
        } catch (const ConfigurationException&) {
            return defaultValue;
        }
    }
};

#endif // ETL_PLUS_COMPATIBILITY_CONFIG_HPP
EOF

    # Create old WebSocket compatibility header
    cat > "$compat_dir/websocket.hpp" << 'EOF'
#ifndef ETL_PLUS_COMPATIBILITY_WEBSOCKET_HPP
#define ETL_PLUS_COMPATIBILITY_WEBSOCKET_HPP

// Compatibility layer for old WebSocket system
// This file provides backward compatibility for code still using old WebSocket patterns

#include "websocket_manager.hpp"
#include <string>
#include <functional>

// Compatibility class for old WebSocketHandler interface
class WebSocketHandler {
public:
    virtual ~WebSocketHandler() = default;
    virtual void onMessage(const std::string& message) = 0;
    virtual void onConnect(void* connection) = 0;
    virtual void onDisconnect(void* connection) = 0;
};

// Adapter class to bridge old and new WebSocket interfaces
class WebSocketCompatibilityAdapter : public WebSocketManager::Handler {
private:
    WebSocketHandler* legacyHandler_;
    std::string correlationId_;

public:
    explicit WebSocketCompatibilityAdapter(WebSocketHandler* legacyHandler,
                                         const std::string& correlationId = "legacy")
        : legacyHandler_(legacyHandler), correlationId_(correlationId) {}

    void onConnection(const WebSocketConnectionPtr& conn,
                     const std::string& correlationId) override {
        if (legacyHandler_) {
            legacyHandler_->onConnect(conn.get());
        }
    }

    void onDisconnection(const WebSocketConnectionPtr& conn,
                        const std::string& correlationId) override {
        if (legacyHandler_) {
            legacyHandler_->onDisconnect(conn.get());
        }
    }

    void handleMessage(const std::string& message, const std::string& correlationId) {
        if (legacyHandler_) {
            legacyHandler_->onMessage(message);
        }
    }
};

#endif // ETL_PLUS_COMPATIBILITY_WEBSOCKET_HPP
EOF

    log_success "Created compatibility header files in $compat_dir"
}

# Create compatibility source files
create_compatibility_sources() {
    log_info "Creating compatibility source files..."

    local compat_dir="$TARGET_DIR/src/compatibility"
    mkdir -p "$compat_dir"

    # Create implementation file for compatibility layer
    cat > "$compat_dir/compatibility.cpp" << 'EOF'
// ETL Plus Compatibility Layer Implementation
// Provides backward compatibility for legacy code patterns

#include "compatibility/exceptions.hpp"
#include "compatibility/logging.hpp"
#include "compatibility/config.hpp"
#include "compatibility/websocket.hpp"

// This file ensures all compatibility classes are properly instantiated
// and can be linked by legacy code

// Force instantiation of compatibility classes
template class ComponentLogger<struct LegacyLogger>;
template class ComponentLogger<Logger>;
EOF

    log_success "Created compatibility source files in $compat_dir"
}

# Update CMakeLists.txt to include compatibility layer
update_cmake() {
    log_info "Updating CMakeLists.txt for compatibility layer..."

    local cmake_file="$TARGET_DIR/CMakeLists.txt"

    if [[ -f "$cmake_file" ]]; then
        # Add compatibility include directory
        if ! grep -q "include/compatibility" "$cmake_file" 2>/dev/null; then
            sed -i.bak '/include_directories(/a\
    ${CMAKE_SOURCE_DIR}/include/compatibility
' "$cmake_file"
        fi

        # Add compatibility source files
        if ! grep -q "src/compatibility" "$cmake_file" 2>/dev/null; then
            sed -i.bak '/add_executable.*src\//a\
    src/compatibility/compatibility.cpp
' "$cmake_file"
        fi

        log_success "Updated CMakeLists.txt with compatibility layer"
    else
        log_warning "CMakeLists.txt not found - manual CMake configuration required"
    fi
}

# Create compatibility documentation
create_compatibility_docs() {
    log_info "Creating compatibility layer documentation..."

    cat > "$TARGET_DIR/docs/compatibility_layer.md" << 'EOF'
# ETL Plus Compatibility Layer

## Overview

The compatibility layer provides backward compatibility for code that has not yet been migrated to the new ETL Plus architecture. It allows legacy code to continue functioning while migration is in progress.

## Included Compatibility Features

### Exception Compatibility

Legacy exception types are mapped to the new ETLException system:

```cpp
// Old code (still works)
throw DatabaseException("Connection failed");

// New code (recommended)
throw ETLException("Database error: Connection failed", correlationId, "Connection failed");
```

### Logging Compatibility

Old logging macros are mapped to the new ComponentLogger system:

```cpp
// Old code (still works)
LOG_INFO("Processing started");
LOG_ERROR("Processing failed");

// New code (recommended)
logger_->info("Processing started", correlationId);
logger_->error("Processing failed", correlationId);
```

### Configuration Compatibility

Old configuration access patterns are mapped to the new template-based system:

```cpp
// Old code (still works)
std::string host = config->getString("database.host");
int port = config->getInt("database.port");

// New code (recommended)
std::string host = config->get<std::string>("database.host");
int port = config->get<int>("database.port");
```

### WebSocket Compatibility

Old WebSocket handler interface is adapted to the new WebSocketManager system:

```cpp
// Old code (still works with adapter)
class MyHandler : public WebSocketHandler {
    void onMessage(const std::string& msg) override {
        // Handle message
    }
};

// New code (recommended)
class MyHandler : public WebSocketManager::Handler {
    void handleMessage(const std::string& msg, const std::string& correlationId) override {
        // Handle message
    }
};
```

## Usage

To use the compatibility layer:

1. Include the appropriate compatibility header:
   ```cpp
   #include "compatibility/exceptions.hpp"
   #include "compatibility/logging.hpp"
   #include "compatibility/config.hpp"
   #include "compatibility/websocket.hpp"
   ```

2. Existing code should continue to work without changes

3. Gradually migrate to new patterns as time permits

## Migration Strategy

1. **Phase 1**: Install compatibility layer
2. **Phase 2**: Migrate critical components first
3. **Phase 3**: Remove compatibility layer once all code is migrated

## Removal

Once all legacy code has been migrated, remove the compatibility layer by:

1. Removing compatibility includes from source files
2. Removing compatibility directory from CMakeLists.txt
3. Deleting the compatibility files

## Warning

The compatibility layer is intended as a temporary measure during migration. It may not provide full feature parity with the new system and should be removed once migration is complete.
EOF

    log_success "Created compatibility layer documentation"
}

# Generate compatibility report
generate_compatibility_report() {
    log_info "Generating compatibility layer installation report..."

    cat << EOF
============================================================
Compatibility Layer Installation Report
Generated: $(date)
============================================================

COMPATIBILITY FEATURES INSTALLED:
-------------------------------
EOF

    local compat_dir="$TARGET_DIR/include/compatibility"
    if [[ -d "$compat_dir" ]]; then
        echo "✅ Compatibility headers installed:"
        ls -1 "$compat_dir"/*.hpp 2>/dev/null | while read -r file; do
            echo "   - $(basename "$file")"
        done
    fi

    local compat_src_dir="$TARGET_DIR/src/compatibility"
    if [[ -d "$compat_src_dir" ]]; then
        echo "✅ Compatibility sources installed:"
        ls -1 "$compat_src_dir"/*.cpp 2>/dev/null | while read -r file; do
            echo "   - $(basename "$file")"
        done
    fi

    if [[ -f "$TARGET_DIR/docs/compatibility_layer.md" ]]; then
        echo "✅ Documentation created: docs/compatibility_layer.md"
    fi

    cat << EOF

COMPATIBILITY FEATURES:
---------------------
• Exception compatibility (DatabaseException, NetworkException, ValidationException)
• Logging compatibility (LOG_INFO, LOG_ERROR, LOG_DEBUG macros)
• Configuration compatibility (getString, getInt, getBool methods)
• WebSocket compatibility (WebSocketHandler adapter)

USAGE INSTRUCTIONS:
------------------
1. Include compatibility headers in legacy code:
   #include "compatibility/exceptions.hpp"
   #include "compatibility/logging.hpp"
   #include "compatibility/config.hpp"
   #include "compatibility/websocket.hpp"

2. Existing code should work without changes

3. Gradually migrate to new patterns:
   - Replace old exceptions with ETLException
   - Replace logging macros with ComponentLogger
   - Use template-based config access
   - Update WebSocket handlers to new interface

MIGRATION TIMELINE:
------------------
• Phase 1 (Immediate): Use compatibility layer
• Phase 2 (Weeks 1-2): Migrate critical components
• Phase 3 (Weeks 2-3): Remove compatibility layer

WARNING:
--------
The compatibility layer is temporary. Remove it once
all legacy code has been migrated to prevent technical debt.

============================================================
EOF
}

# Main execution
main() {
    parse_args "$@"

    log_info "Installing ETL Plus compatibility layer..."
    log_info "Target directory: $TARGET_DIR"
    [[ $DRY_RUN == true ]] && log_info "Running in dry-run mode"

    if [[ $DRY_RUN == false ]]; then
        create_compatibility_headers
        create_compatibility_sources
        update_cmake
        create_compatibility_docs
    else
        log_info "Would create compatibility headers (dry run)"
        log_info "Would create compatibility sources (dry run)"
        log_info "Would update CMakeLists.txt (dry run)"
        log_info "Would create documentation (dry run)"
    fi

    generate_compatibility_report

    log_success "Compatibility layer installation completed!"
}

# Run main function
main "$@"
