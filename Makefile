# ETL Plus Makefile
PROJECT_NAME = ETLPlusBackend
BUILD_DIR = build
BIN_DIR = $(BUILD_DIR)/bin

# Colors
RED = \033[0;31m
GREEN = \033[0;32m
YELLOW = \033[1;33m
BLUE = \033[0;34m
NC = \033[0m

.PHONY: all configure compile clean rebuild run help status docker-up docker-down

all: compile

help:
	@echo "ETL Plus Build System"
	@echo "===================="
	@echo ""
	@echo "Commands:"
	@echo "  make          - Build the project"
	@echo "  make clean    - Clean build files"
	@echo "  make run      - Run the application"
	@echo "  make status   - Show project status"
	@echo "  make help     - Show this help"

$(BUILD_DIR):
	@echo "$(BLUE)Creating build directory...$(NC)"
	@mkdir -p $(BUILD_DIR)

configure: $(BUILD_DIR)
	@echo "$(BLUE)Configuring CMake...$(NC)"
	@cd $(BUILD_DIR) && cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug ..

compile: configure
	@echo "$(BLUE)Building $(PROJECT_NAME)...$(NC)"
	@cd $(BUILD_DIR) && make -j4
	@echo "$(GREEN)Build complete!$(NC)"

clean:
	@echo "$(YELLOW)Cleaning...$(NC)"
	@rm -rf $(BUILD_DIR)

rebuild: clean compile

run: compile
	@echo "$(BLUE)Running $(PROJECT_NAME)...$(NC)"
	@cd $(BIN_DIR) && ./$(PROJECT_NAME)

status:
	@echo "ETL Plus Project Status"
	@echo "======================"
	@echo "Project: $(PROJECT_NAME)"
	@if [ -f $(BIN_DIR)/$(PROJECT_NAME) ]; then \
		echo "Status: Built"; \
	else \
		echo "Status: Not built"; \
	fi

docker-up:
	@echo "$(BLUE)Starting Oracle database...$(NC)"
	@docker-compose up -d oracle-db

docker-down:
	@echo "$(YELLOW)Stopping Oracle database...$(NC)"
	@docker-compose down
