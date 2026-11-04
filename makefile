CC       ?= gcc
OPT      ?= -O2
CFLAGS   ?= -std=c99 -Wall -Wextra -pedantic -g $(OPT)
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDLIBS   += -lm

SRC_DIR   = src
BUILD_DIR = build

# Everything except main.c is built into a static library that the server,
# the tests and the benchmarks all link against.
LIB_SRCS = $(filter-out $(SRC_DIR)/main.c,$(wildcard $(SRC_DIR)/*.c))
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))
LIB      = $(BUILD_DIR)/libpulsedb.a

TARGET     = pulsedb
TEST_SRCS  = $(wildcard tests/*.c)
TEST_BIN   = $(BUILD_DIR)/run_tests
BENCH_BIN  = $(BUILD_DIR)/bench

.PHONY: all clean test bench sanitize

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(TARGET): $(SRC_DIR)/main.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS) $(LDLIBS)

$(TEST_BIN): $(TEST_SRCS) $(wildcard tests/*.h) $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(SRC_DIR) -o $@ $(TEST_SRCS) $(LIB) $(LDFLAGS) $(LDLIBS)

$(BENCH_BIN): bench/bench.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(SRC_DIR) -o $@ $< $(LIB) $(LDFLAGS) $(LDLIBS)

# Unit tests plus end-to-end tests against a real server process.
test: $(TARGET) $(TEST_BIN)
	./$(TEST_BIN) ./$(TARGET)

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

# Rebuild everything with AddressSanitizer and UBSan, then run the tests.
sanitize:
	$(MAKE) clean
	$(MAKE) test OPT="-O1 -fsanitize=address,undefined -fno-omit-frame-pointer" \
	        LDFLAGS="-fsanitize=address,undefined"

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
