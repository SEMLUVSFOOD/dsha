# SPDX-License-Identifier: MIT
#
# Builds and runs the desktop tests of the portable core. Requires gcc and
# python3 (for the fake Home Assistant server).

CC      ?= gcc
CFLAGS  ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
           -fsanitize=address,undefined -fno-omit-frame-pointer \
           -Wno-format-truncation
CFLAGS  += -I../source/core
LDFLAGS ?= -fsanitize=address,undefined

CORE    := $(filter-out ../source/core/platform_nds.c,$(wildcard ../source/core/*.c))
COMMON  := $(CORE) host_platform.c

BUILD   := build

.PHONY: all test clean screenshots

all: $(BUILD)/test_core $(BUILD)/test_http $(BUILD)/test_discover $(BUILD)/test_setup $(BUILD)/render

$(BUILD)/%: %.c $(COMMON) $(wildcard ../source/core/*.h) testlib.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(COMMON) $(LDFLAGS) -lm

test: all
	$(BUILD)/test_core
	python3 run_http_tests.py $(BUILD)/test_http
	python3 run_http_tests.py --discover $(BUILD)/test_discover
	python3 run_http_tests.py --setup $(BUILD)/test_setup

screenshots: $(BUILD)/render
	python3 run_http_tests.py --screenshots $(BUILD)/render ../docs/screenshots

clean:
	rm -rf $(BUILD)
