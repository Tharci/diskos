/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* BlueZ's own record of paired devices, read straight from its storage directory: no bluetoothd round-trip, so the
 * Bluetooth screen can list them at once, even while the radio is off or still starting. Pure C (host-tested). */
#ifndef DISKOS_BTSTORE_H
#define DISKOS_BTSTORE_H
#define BTSTORE_ROOT "/usr/data/storebluetooth/lib/bluetooth"   /* the stock bluetoothd's STORAGEDIR (V2.57) */
typedef struct { char mac[18]; char name[249]; int trusted; } btstore_dev_t;
/* Paired audio sinks recorded under root/<adapter>/<device>/info, sorted by name (the alias when one is set). Returns
 * the count, at most cap; 0 when root is missing or unreadable. */
int btstore_paired_audio(const char *root, btstore_dev_t *out, int cap);
#endif
