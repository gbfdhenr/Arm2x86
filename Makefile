CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unused-function -O2 -fPIC -I. -std=c11
LDFLAGS = -shared
LDLIBS  = -ldl -lpthread
TARGET  = libarm2x86.so

# Main integration file that includes all modules via #include
SRC     = arm2x86.c
OBJ     = $(SRC:.c=.o)

# Test configuration
TEST_DIR = tests
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(TEST_DIR)/%,$(TEST_SRCS))
TEST_RUNNER = $(TEST_DIR)/run_tests

.PHONY: all clean debug avx perf test test-clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c arm2x86.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Build tests
test: $(TARGET) $(TEST_RUNNER)

$(TEST_RUNNER): $(TEST_SRCS)
	$(CC) $(CFLAGS) -I./include -I. -o $@ $^ -L. -larm2x86 $(LDLIBS) -lrt

# Run tests
run-test: test
	@echo "Running tests..."
	./$(TEST_RUNNER)

test-clean:
	rm -f $(TEST_BINS) $(TEST_RUNNER)

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
