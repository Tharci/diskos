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

# The bundled SQLite amalgamation, with the device build's behaviour flags (ui/Makefile SQLITE_FLAGS). Built once and
# without sanitizers: it is third-party and very slow to instrument. Host object name, so it never meets the device's
# ../sqlite3.o.
SQLITE_DEFS := -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DQS=0 \
               -DSQLITE_OMIT_DEPRECATED
sqlite3_host.o: ../sqlite3.c
	$(CC) -O1 -w $(SQLITE_DEFS) -c ../sqlite3.c -o $@

folderscope_test: ../folderscope.c folderscope_test.c ../folderscope.h sqlite3_host.o
	$(CC) $(CFLAGS) -I.. ../folderscope.c folderscope_test.c sqlite3_host.o -lpthread -lm -o $@

# The scanner's tag readers: wavtags_test.c includes ../scanner.c so its static readers are reachable, and the link
# drops everything they don't use (no SQLite, no UI).
wavtags_test: ../scanner.c ../scanner.h ../audiodur.h wavtags_test.c
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -I.. wavtags_test.c -Wl,--gc-sections -o $@

# The volume keys' press/hold state machine (volkeys_hold.c is pure; volkeys.c, the device side, is not built here).
volkeys_hold_test: ../volkeys_hold.c ../volkeys_hold.h volkeys_hold_test.c
	$(CC) $(CFLAGS) -I.. ../volkeys_hold.c volkeys_hold_test.c -o $@

# BlueZ's paired-device store reader (btstore.c is pure; bt.c, the screen, is not built here).
btstore_test: ../btstore.c ../btstore.h btstore_test.c
	$(CC) $(CFLAGS) -I.. ../btstore.c btstore_test.c -o $@

.PHONY: run clean
run: config_test playstate_test folderscope_test wavtags_test volkeys_hold_test btstore_test
	./config_test
	@echo "--- playstate ---"
	./playstate_test
	@echo "--- folderscope ---"
	./folderscope_test
	@echo "--- wavtags ---"
	./wavtags_test
	@echo "--- volkeys_hold ---"
	./volkeys_hold_test
	@echo "--- btstore ---"
	./btstore_test

clean:
	rm -f config_test playstate_test folderscope_test wavtags_test volkeys_hold_test btstore_test sqlite3_host.o
