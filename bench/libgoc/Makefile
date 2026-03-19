.PHONY: all run build clean

ROOT_DIR := $(abspath ../..)
BUILD_DIR := $(ROOT_DIR)/build-bench
BENCH_BIN := bench-libgoc
PKG_CONFIG ?= pkg-config

CFLAGS := -std=c11 -O2 -DNDEBUG -DGC_THREADS -D_GNU_SOURCE
CFLAGS += -I$(ROOT_DIR)/include -I$(ROOT_DIR)/src -I$(ROOT_DIR)/vendor/minicoro
CFLAGS += $(shell $(PKG_CONFIG) --cflags libuv bdw-gc-threaded)
LDLIBS := $(shell $(PKG_CONFIG) --libs libuv bdw-gc-threaded)

all: build

$(BUILD_DIR)/libgoc.a:
	cmake -S $(ROOT_DIR) -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) --target goc

build: $(BENCH_BIN)

$(BENCH_BIN): bench.c $(BUILD_DIR)/libgoc.a
	$(CC) $(CFLAGS) $< $(BUILD_DIR)/libgoc.a $(LDLIBS) -o $@

run: $(BENCH_BIN)
	./$(BENCH_BIN)

clean:
	rm -f $(BENCH_BIN)
