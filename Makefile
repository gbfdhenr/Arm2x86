CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unused-function -O2 -fPIC -I. -std=c11 -fno-omit-frame-pointer
LDFLAGS = -shared
LDLIBS  = -ldl -lpthread -lrt

# Version
VERSION = 1.0.1-pre1

# Output directory
BUILD_DIR = $(CURDIR)/build
TARGET_NAME = libarm2x86.so.$(VERSION)
TARGET = $(BUILD_DIR)/$(TARGET_NAME)

# Main integration file that includes all modules via #include
SRC     = arm2x86.c
OBJ     = $(SRC:.c=.o)
ASM_SRC = modules/arm2x86_call_with_reg_home.S
ASM_OBJ = $(ASM_SRC:.S=.o)

# Test configuration
TEST_DIR = tests
TEST_SRCS = $(wildcard $(TEST_DIR)/test_basic.c)
TEST_SVE_SRCS = $(wildcard $(TEST_DIR)/test_sve.c)
TEST_RUNNER = $(TEST_DIR)/run_tests
TEST_SVE_RUNNER = $(TEST_DIR)/run_sve_tests

.PHONY: all clean debug avx perf test test-clean release

all: $(TARGET)

$(TARGET): $(OBJ) $(ASM_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@ln -sf $(TARGET_NAME) $(BUILD_DIR)/libarm2x86.so
	@ln -sf $(TARGET_NAME) $(BUILD_DIR)/libarm2x86.so.1
	@echo "Built: $(TARGET)"
	@echo "Symlinks: $(BUILD_DIR)/libarm2x86.so -> $(TARGET_NAME)"
	@echo "          $(BUILD_DIR)/libarm2x86.so.1 -> $(TARGET_NAME)"

%.o: %.c arm2x86.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

# Release build target (explicit)
release: CFLAGS := -Wall -Wextra -Wno-unused-function -O2 -fPIC -I. -std=c11 -fno-omit-frame-pointer -DNDEBUG
release: $(TARGET)

# Build all tests
test: $(TARGET) $(TEST_RUNNER) $(TEST_SVE_RUNNER)

$(TEST_RUNNER): $(TEST_SRCS) $(TARGET)
	$(CC) $(CFLAGS) -I./include -I. -o $@ $(TEST_SRCS) -L. -larm2x86 $(LDLIBS)

$(TEST_SVE_RUNNER): $(TEST_SVE_SRCS) $(TARGET)
	$(CC) $(CFLAGS) -I./include -I. -o $@ $(TEST_SVE_SRCS) -L. -larm2x86 $(LDLIBS)

# Run all tests
run-test: test
	@echo "Running basic tests..."
	LD_LIBRARY_PATH=. ./$(TEST_RUNNER)
	@echo "Running SVE tests..."
	LD_LIBRARY_PATH=. ./$(TEST_SVE_RUNNER)

test-clean:
	rm -f $(TEST_RUNNER) $(TEST_SVE_RUNNER)

clean: test-clean
	rm -f $(OBJ) $(TARGET)

# Build with debug info
debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

# Build with AVX support
avx: CFLAGS += -mavx
avx: $(TARGET)

# Build with performance monitoring enabled
perf: CFLAGS += -DARM2X86_ENABLE_PERF
perf: $(TARGET)

# Build with all debug flags
debug-all: CFLAGS += -DARM2X86_DEBUG_DECODE -DARM2X86_DEBUG_TRANSLATION \
                     -DARM2X86_DEBUG_THUMB -DARM2X86_DEBUG_NEON \
                     -DARM2X86_DEBUG_CACHE -DARM2X86_DEBUG_PERF
debug-all: $(TARGET)
