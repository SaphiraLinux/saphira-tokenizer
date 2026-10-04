# Scalar C GPT-2/r50k tokenizer — plain C11, no SIMD, no optimisation.
#
# Correctness first: the oracle exists to match the hardened Rust tokenizer
# byte for byte, so the default build is -O0 with the full warning set. Fast
# flags come later and only after parity is frozen.

CC      ?= cc
WARN     = -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion \
           -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS  ?= -std=c11 -O0 -g $(WARN)
CPPFLAGS += -Iinclude
LDLIBS   = -lm

BUILD  ?= build
SRC     = $(wildcard lib/*.c)
OBJ     = $(patsubst lib/%.c,$(BUILD)/%.o,$(SRC))
TOOL    = $(BUILD)/gt_dump_ids
DEBUG   = $(BUILD)/gt_debug_stages
BENCH   = $(BUILD)/gt_bench
TEST    = $(BUILD)/gt_selftest

.PHONY: all tool test check tables asan clean

all: $(TOOL) $(TEST) $(DEBUG) $(BENCH)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: lib/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TOOL): $(BUILD)/gt_dump_ids.o $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(TEST): $(BUILD)/gt_selftest.o $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(DEBUG): $(BUILD)/gt_debug_stages.o $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BENCH): $(BUILD)/gt_bench.o $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS) -pthread

$(BUILD)/%.o: tools/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_GNU_SOURCE -c $< -o $@

$(BUILD)/%.o: tests/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

tool: $(TOOL)
test: $(TEST)
check: $(TEST) $(TOOL)

# Regenerate the Unicode classification tables from the UCD files in data/.
tables:
	python3 tools/gen_unicode_tables.py

# Address + undefined-behaviour sanitizers. Same code, different instrumentation.
asan:
	$(MAKE) clean
	$(MAKE) BUILD=build-asan \
	        CFLAGS="-std=c11 -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer" \
	        LDFLAGS="-fsanitize=address,undefined" all

# Optimised build: -O2, AVX2, LTO. Same source, same selftest, same checksum.
# The default `all` stays -O0 for oracle verification; use this for speed.
opt:
	$(MAKE) BUILD=build-opt \
	        CFLAGS="-std=c11 -O2 -g -mavx2 -flto $(WARN)" \
	        LDFLAGS="-flto" all

clean:
	rm -rf build build-asan
