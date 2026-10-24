# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# Off-device host tests for diskOS pure-logic modules (arch#5 harness).
# Native x86-64 build with sanitizers + CFG_* paths pointed at a temp dir. Does NOT touch the
# device build (separate dir, separate Makefile). Run: make -C tests run
CC ?= cc
CFLAGS := -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
TDIR := /tmp/diskos_cfgtest
DEFS := -DCFG_PATH='"$(TDIR)/diskos.conf"' -DCFG_DIR='"$(TDIR)"' \
        -DLEGACY_CONF='"$(TDIR)/nolegacy1"' -DLEGACY_SWIPE='"$(TDIR)/nolegacy2"'

config_test: ../config.c config_test.c ../config.h
	$(CC) $(CFLAGS) $(DEFS) -I.. ../config.c config_test.c -o $@

playstate_test: ../playstate.c playstate_test.c ../playstate.h
	$(CC) $(CFLAGS) -I.. ../playstate.c playstate_test.c -o $@

# The scanner's tag readers: wavtags_test.c includes ../scanner.c so its static readers are reachable, and the link
# drops everything they don't use (no SQLite, no UI).
wavtags_test: ../scanner.c ../scanner.h ../audiodur.h wavtags_test.c
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -I.. wavtags_test.c -Wl,--gc-sections -o $@

.PHONY: run clean
run: config_test playstate_test wavtags_test
	./config_test
	@echo "--- playstate ---"
	./playstate_test
	@echo "--- wavtags ---"
	./wavtags_test

clean:
	rm -f config_test playstate_test wavtags_test
