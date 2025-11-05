# Makefile for FIndex - Fast File Integrity Checker

# Compiler and flags
CC       = gcc
CFLAGS   = -Wall -Wextra -O3 -march=native -pthread -D_GNU_SOURCE -D_XOPEN_SOURCE=700
LDFLAGS  = -pthread
DBGFLAGS = -g -O0 -DDEBUG

# Installation paths
PREFIX   = /usr/local
BINDIR   = $(PREFIX)/bin
MANDIR   = $(PREFIX)/share/man/man1

# Target executable
TARGET   = findex
SOURCE   = main.c

# Colors for output (optional)
RED      = \033[0;31m
GREEN    = \033[0;32m
YELLOW   = \033[0;33m
CYAN     = \033[0;36m
NC       = \033[0m # No Color

.PHONY: all clean install uninstall debug test help

# Default target
all: $(TARGET)

# Build main executable
$(TARGET): $(SOURCE)
	@echo "$(CYAN)Building $(TARGET)...$(NC)"
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)
	@echo "$(GREEN)✓ Build complete: $(TARGET)$(NC)"

# Debug build
debug: CFLAGS += $(DBGFLAGS)
debug: clean $(TARGET)
	@echo "$(YELLOW)Debug build complete$(NC)"

# Install to system
install: $(TARGET)
	@echo "$(CYAN)Installing $(TARGET) to $(BINDIR)...$(NC)"
	@mkdir -p $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	@echo "$(GREEN)✓ Installed to $(BINDIR)/$(TARGET)$(NC)"
	@echo "$(CYAN)Run '$(TARGET) --help' to get started$(NC)"

# Uninstall from system
uninstall:
	@echo "$(CYAN)Uninstalling $(TARGET)...$(NC)"
	@rm -f $(BINDIR)/$(TARGET)
	@echo "$(GREEN)✓ Uninstalled$(NC)"

# Clean build artifacts
clean:
	@echo "$(CYAN)Cleaning build artifacts...$(NC)"
	@rm -f $(TARGET)
	@rm -f *.o
	@rm -f core
	@rm -f vgcore.*
	@echo "$(GREEN)✓ Clean complete$(NC)"

# Clean everything including database and logs
cleanall: clean
	@echo "$(CYAN)Cleaning database and logs...$(NC)"
	@rm -f *.db
	@rm -f *.log
	@echo "$(GREEN)✓ All clean$(NC)"

# Static analysis with various tools
analyze:
	@echo "$(CYAN)Running static analysis...$(NC)"
	@which cppcheck >/dev/null 2>&1 && cppcheck --enable=all --suppress=missingIncludeSystem $(SOURCE) || echo "$(YELLOW)cppcheck not found, skipping$(NC)"
	@which clang-tidy >/dev/null 2>&1 && clang-tidy $(SOURCE) -- $(CFLAGS) || echo "$(YELLOW)clang-tidy not found, skipping$(NC)"

# Memory leak check (requires valgrind)
memcheck: $(TARGET)
	@echo "$(CYAN)Running memory leak check...$(NC)"
	@which valgrind >/dev/null 2>&1 && \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		./$(TARGET) --index && \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		./$(TARGET) --check || \
		echo "$(YELLOW)valgrind not found, install it for memory checks$(NC)"

# Quick test
test: $(TARGET)
	@echo "$(CYAN)Running basic tests...$(NC)"
	@echo "$(YELLOW)Test 1: Index current directory$(NC)"
	@./$(TARGET) --index --db test.db --threads 2
	@echo "$(YELLOW)Test 2: Check against database$(NC)"
	@./$(TARGET) --check --db test.db --threads 2 --filter all
	@echo "$(YELLOW)Test 3: Verify database file exists$(NC)"
	@test -f test.db && echo "$(GREEN)✓ Database created$(NC)" || echo "$(RED)✗ Database not found$(NC)"
	@echo "$(YELLOW)Test 4: Verify log file exists$(NC)"
	@test -f findex.log && echo "$(GREEN)✓ Log file created$(NC)" || echo "$(RED)✗ Log file not found$(NC)"
	@rm -f test.db
	@echo "$(GREEN)✓ Tests complete$(NC)"

# Benchmark
benchmark: $(TARGET)
	@echo "$(CYAN)Running benchmark...$(NC)"
	@echo "$(YELLOW)Indexing current directory...$(NC)"
	@time ./$(TARGET) --index --db bench.db
	@echo "$(YELLOW)Checking against database...$(NC)"
	@time ./$(TARGET) --check --db bench.db
	@rm -f bench.db
	@echo "$(GREEN)✓ Benchmark complete$(NC)"

# Format code (requires clang-format)
format:
	@echo "$(CYAN)Formatting code...$(NC)"
	@which clang-format >/dev/null 2>&1 && \
		clang-format -i $(SOURCE) && \
		echo "$(GREEN)✓ Code formatted$(NC)" || \
		echo "$(YELLOW)clang-format not found, skipping$(NC)"

# Show help
help:
	@echo "$(CYAN)FIndex - Fast File Integrity Checker$(NC)"
	@echo ""
	@echo "$(GREEN)Available targets:$(NC)"
	@echo "  $(YELLOW)make$(NC)              - Build the executable (default)"
	@echo "  $(YELLOW)make debug$(NC)        - Build with debug symbols"
	@echo "  $(YELLOW)make install$(NC)      - Install to $(BINDIR)"
	@echo "  $(YELLOW)make uninstall$(NC)    - Remove from system"
	@echo "  $(YELLOW)make clean$(NC)        - Remove build artifacts"
	@echo "  $(YELLOW)make cleanall$(NC)     - Remove build artifacts, databases, and logs"
	@echo "  $(YELLOW)make test$(NC)         - Run basic tests"
	@echo "  $(YELLOW)make benchmark$(NC)    - Run performance benchmark"
	@echo "  $(YELLOW)make analyze$(NC)      - Run static analysis (requires cppcheck, clang-tidy)"
	@echo "  $(YELLOW)make memcheck$(NC)     - Check for memory leaks (requires valgrind)"
	@echo "  $(YELLOW)make format$(NC)       - Format code (requires clang-format)"
	@echo "  $(YELLOW)make help$(NC)         - Show this help message"
	@echo ""
	@echo "$(GREEN)Installation prefix:$(NC)"
	@echo "  Default: $(PREFIX)"
	@echo "  Custom:  make install PREFIX=/opt/local"
	@echo ""
	@echo "$(GREEN)Build options:$(NC)"
	@echo "  CC=clang make       - Use Clang compiler"
	@echo "  CFLAGS=-O2 make     - Custom optimization level"
	@echo ""
	@echo "$(GREEN)Examples:$(NC)"
	@echo "  make                 # Build"
	@echo "  make test            # Build and test"
	@echo "  sudo make install    # Install system-wide"
	@echo "  make clean           # Clean up"

# Info about the build
info:
	@echo "$(CYAN)Build Information:$(NC)"
	@echo "  Compiler:    $(CC)"
	@echo "  CFLAGS:      $(CFLAGS)"
	@echo "  LDFLAGS:     $(LDFLAGS)"
	@echo "  Target:      $(TARGET)"
	@echo "  Install dir: $(BINDIR)"
	@echo "  Source:      $(SOURCE)"

# Check dependencies
check-deps:
	@echo "$(CYAN)Checking dependencies...$(NC)"
	@which $(CC) >/dev/null 2>&1 && echo "$(GREEN)✓ $(CC) found$(NC)" || echo "$(RED)✗ $(CC) not found$(NC)"
	@echo "$(YELLOW)Optional tools:$(NC)"
	@which valgrind >/dev/null 2>&1 && echo "$(GREEN)✓ valgrind found$(NC)" || echo "  valgrind not found (optional)"
	@which cppcheck >/dev/null 2>&1 && echo "$(GREEN)✓ cppcheck found$(NC)" || echo "  cppcheck not found (optional)"
	@which clang-tidy >/dev/null 2>&1 && echo "$(GREEN)✓ clang-tidy found$(NC)" || echo "  clang-tidy not found (optional)"
	@which clang-format >/dev/null 2>&1 && echo "$(GREEN)✓ clang-format found$(NC)" || echo "  clang-format not found (optional)"