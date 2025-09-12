# ETL Plus Makefile with Ninja and Mac Optimizations
PROJECT_NAME = ETLPlusBackend
BUILD_DIR = build
BIN_DIR = $(BUILD_DIR)/bin

# Detect OS and set build system accordingly
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # Mac: Use Ninja if available, otherwise make
    GENERATOR := $(shell which ninja > /dev/null 2>&1 && echo "-GNinja" || echo "")
    BUILD_COMMAND := $(shell which ninja > /dev/null 2>&1 && echo "ninja" || echo "make -j$$(sysctl -n hw.ncpu)")
    BUILD_SYSTEM := $(shell which ninja > /dev/null 2>&1 && echo "Ninja" || echo "Make")
    CPU_COUNT := $(shell sysctl -n hw.ncpu)
else
    # Linux/Others: Use Ninja if available
    GENERATOR := $(shell which ninja > /dev/null 2>&1 && echo "-GNinja" || echo "")
    BUILD_COMMAND := $(shell which ninja > /dev/null 2>&1 && echo "ninja" || echo "make -j$$(nproc)")
    BUILD_SYSTEM := $(shell which ninja > /dev/null 2>&1 && echo "Ninja" || echo "Make")
    CPU_COUNT := $(shell nproc)
endif

# Colors
RED = \033[0;31m
GREEN = \033[0;32m
YELLOW = \033[1;33m
BLUE = \033[0;34m
CYAN = \033[0;36m
NC = \033[0m

.PHONY: all configure compile clean rebuild run help status ninja-info build-info format format-check

all: build-info format compile

help:
	@echo "ETL Plus Build System"
	@echo "===================="
	@echo ""
	@echo "$(CYAN)System Info:$(NC)"
	@echo "  OS: $(UNAME_S)"
	@echo "  Build System: $(BUILD_SYSTEM)"
	@echo "  CPU Cores: $(CPU_COUNT)"
	@echo ""
	@echo "$(CYAN)Commands:$(NC)"
	@echo "  make              - Build the project (with formatting)"
	@echo "  make format       - Run clang-format on all source files"
	@echo "  make format-check - Check if files need formatting"
	@echo "  make clean        - Clean build files"
	@echo "  make rebuild      - Clean and rebuild"
	@echo "  make run          - Run the application"
	@echo "  make status       - Show project status"
	@echo "  make help         - Show this help"
	@echo "  make ninja-info   - Check Ninja availability"

build-info:
	@echo "$(CYAN)ETL Plus Build System$(NC)"
	@echo "Using $(BUILD_SYSTEM) on $(UNAME_S) with $(CPU_COUNT) cores"
	@if [ "$(BUILD_SYSTEM)" = "Ninja" ]; then \
		echo "$(GREEN)✓ Ninja detected - using fast parallel builds$(NC)"; \
	else \
		echo "$(YELLOW)⚠ Using Make - consider installing Ninja for faster builds$(NC)"; \
	fi

format:
	@echo "$(BLUE)Running clang-format on all source files...$(NC)"
	@if which clang-format > /dev/null 2>&1; then \
		find . -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i; \
		echo "$(GREEN)✓ Code formatting complete!$(NC)"; \
	else \
		echo "$(YELLOW)⚠ clang-format not found - skipping formatting$(NC)"; \
		echo "Install with: brew install clang-format (macOS) or apt-get install clang-format (Ubuntu)"; \
	fi

format-check:
	@echo "$(BLUE)Checking code formatting...$(NC)"
	@if which clang-format > /dev/null 2>&1; then \
		if find . -name "*.cpp" -o -name "*.hpp" | xargs clang-format --dry-run --Werror > /dev/null 2>&1; then \
			echo "$(GREEN)✓ All files are properly formatted$(NC)"; \
		else \
			echo "$(RED)✗ Some files need formatting. Run 'make format' to fix.$(NC)"; \
			exit 1; \
		fi; \
	else \
		echo "$(YELLOW)⚠ clang-format not found - skipping format check$(NC)"; \
	fi

ninja-info:
	@echo "$(CYAN)Ninja Build System Check$(NC)"
	@if which ninja > /dev/null 2>&1; then \
		echo "$(GREEN)✓ Ninja is installed: $$(which ninja)$(NC)"; \
		echo "Version: $$(ninja --version)"; \
	else \
		echo "$(YELLOW)⚠ Ninja not found$(NC)"; \
		echo "Install with: brew install ninja (macOS) or apt-get install ninja-build (Ubuntu)"; \
	fi

$(BUILD_DIR):
	@echo "$(BLUE)Creating build directory...$(NC)"
	@mkdir -p $(BUILD_DIR)

configure: $(BUILD_DIR)
	@echo "$(BLUE)Configuring CMake with $(BUILD_SYSTEM)...$(NC)"
	@cd $(BUILD_DIR) && cmake $(GENERATOR) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug ..
	@if [ "$(BUILD_SYSTEM)" = "Ninja" ]; then \
		echo "$(GREEN)✓ Configured for Ninja build system$(NC)"; \
	else \
		echo "$(YELLOW)✓ Configured for Make build system$(NC)"; \
	fi

compile: format configure
	@echo "$(BLUE)Building $(PROJECT_NAME) with $(BUILD_SYSTEM)...$(NC)"
	@echo "$(CYAN)Using $(CPU_COUNT) parallel jobs$(NC)"
	@cd $(BUILD_DIR) && $(BUILD_COMMAND)
	@echo "$(GREEN)✓ Build complete!$(NC)"

# Fast incremental build (skip configure if already done)
fast: format
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "$(YELLOW)No previous configuration found, running full configure...$(NC)"; \
		$(MAKE) configure; \
	fi
	@echo "$(BLUE)Fast incremental build...$(NC)"
	@cd $(BUILD_DIR) && $(BUILD_COMMAND)
	@echo "$(GREEN)✓ Fast build complete!$(NC)"

clean:
	@echo "$(YELLOW)Cleaning build directory...$(NC)"
	@rm -rf $(BUILD_DIR)
	@echo "$(GREEN)✓ Clean complete!$(NC)"

rebuild: clean compile

# Release build
release: format
	@echo "$(BLUE)Building Release version...$(NC)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(GENERATOR) -DCMAKE_BUILD_TYPE=Release ..
	@cd $(BUILD_DIR) && $(BUILD_COMMAND)
	@echo "$(GREEN)✓ Release build complete!$(NC)"

run: compile
	@echo "$(BLUE)Running $(PROJECT_NAME)...$(NC)"
	@cd $(BIN_DIR) && ./$(PROJECT_NAME)

# Run specific test
test-%:
	@echo "$(BLUE)Running test: $*$(NC)"
	@if [ -f $(BUILD_DIR)/$* ]; then \
		cd $(BUILD_DIR) && ./$*; \
	else \
		echo "$(RED)Test $* not found. Run 'make compile' first.$(NC)"; \
	fi

status:
	@echo "$(CYAN)ETL Plus Project Status$(NC)"
	@echo "======================"
	@echo "Project: $(PROJECT_NAME)"
	@echo "OS: $(UNAME_S)"
	@echo "Build System: $(BUILD_SYSTEM)"
	@echo "CPU Cores: $(CPU_COUNT)"
	@if [ -f $(BIN_DIR)/$(PROJECT_NAME) ]; then \
		echo "$(GREEN)Status: Built ✓$(NC)"; \
		echo "Executable: $(BIN_DIR)/$(PROJECT_NAME)"; \
		echo "Size: $$(du -h $(BIN_DIR)/$(PROJECT_NAME) | cut -f1)"; \
	else \
		echo "$(YELLOW)Status: Not built$(NC)"; \
	fi
	@if [ -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "$(GREEN)CMake: Configured ✓$(NC)"; \
	else \
		echo "$(YELLOW)CMake: Not configured$(NC)"; \
	fi
