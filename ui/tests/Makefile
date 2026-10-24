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

.PHONY: run clean
run: config_test playstate_test folderscope_test
	./config_test
	@echo "--- playstate ---"
	./playstate_test
	@echo "--- folderscope ---"
	./folderscope_test

clean:
	rm -f config_test playstate_test folderscope_test sqlite3_host.o
