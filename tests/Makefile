# SPDX-License-Identifier: MIT
#
# Builds and runs the desktop tests of the portable core. Requires gcc,
# python3 (for the fake Home Assistant server), openssl (to make test
# certificates) and Mbed TLS 3.6 (libmbedtls-dev on Debian/Ubuntu 25.04+,
# "brew install mbedtls@3" on macOS, or tests/get_mbedtls.sh anywhere).

CC      ?= gcc
# Set SANITIZE= (empty) to build without sanitizers (used by CodeQL)
SANITIZE ?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
           -Wno-format-truncation
CFLAGS  += $(SANITIZE) -I../source/core -DDSHA_TESTING
LDFLAGS ?=
LDFLAGS += $(SANITIZE)

# Mbed TLS: MBEDTLS_PREFIX=/path (with include/ and lib/), or the one built by
# get_mbedtls.sh, or Homebrew's, or the system one.
ifeq ($(MBEDTLS_PREFIX),)
  ifneq ($(wildcard build/mbedtls/include/mbedtls/ssl.h),)
    MBEDTLS_PREFIX := $(CURDIR)/build/mbedtls
  else
    MBEDTLS_PREFIX := $(shell brew --prefix mbedtls@3 2>/dev/null || brew --prefix mbedtls 2>/dev/null)
  endif
endif
LIBS    := -lmbedtls -lmbedx509 -lmbedcrypto -lm
ifneq ($(MBEDTLS_PREFIX),)
  CFLAGS  += -isystem $(MBEDTLS_PREFIX)/include
  # Static libraries when available (AddressSanitizer on macOS doesn't like
  # Homebrew's shared ones)
  ifneq ($(wildcard $(MBEDTLS_PREFIX)/lib/libmbedcrypto.a),)
    LIBS  := $(addprefix $(MBEDTLS_PREFIX)/lib/,libmbedtls.a libmbedx509.a libmbedcrypto.a) -lm
  else
    LDFLAGS += -L$(MBEDTLS_PREFIX)/lib -Wl,-rpath,$(MBEDTLS_PREFIX)/lib
  endif
endif

CORE    := $(filter-out ../source/core/platform_nds.c,$(wildcard ../source/core/*.c))
COMMON  := $(CORE) host_platform.c

BUILD   := build

.PHONY: all test clean screenshots

all: $(BUILD)/test_core $(BUILD)/test_http $(BUILD)/test_discover $(BUILD)/test_setup $(BUILD)/fuzz $(BUILD)/render

$(BUILD)/%: %.c $(COMMON) $(wildcard ../source/core/*.h) testlib.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(COMMON) $(LDFLAGS) $(LIBS)

test: all
	$(BUILD)/test_core
	$(BUILD)/fuzz 300000
	python3 run_http_tests.py $(BUILD)/test_http
	python3 run_http_tests.py --tls $(BUILD)/test_http
	python3 run_http_tests.py --discover $(BUILD)/test_discover
	python3 run_http_tests.py --setup $(BUILD)/test_setup

screenshots: $(BUILD)/render
	python3 run_http_tests.py --screenshots $(BUILD)/render ../docs/screenshots

clean:
	rm -rf $(BUILD)
