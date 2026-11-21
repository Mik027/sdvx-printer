// Copyright (C) 2026 Mik027.
// SPDX-License-Identifier: GPL-3.0-only
// Mitsubishi CPM50D (mitsu70x) state machine.
#ifndef EMU_PROTOCOL_H
#define EMU_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PROTO_RESP_MAX 1024

uint32_t plat_now_ms(void);

void proto_init(void);
void proto_service(void);

// EP1 OUT: commands and sometimes the image. EP3 OUT: status commands,
// and the image when it does not follow the header on EP1.
// Replies are delivered through proto_emit() on EP2 IN.
void proto_process_ep1(const uint8_t *data, uint32_t len);
void proto_process_ep3(const uint8_t *data, uint32_t len);

// Implemented in main.c: queue `len` bytes on EP2 IN.
void proto_emit(const uint8_t *data, uint32_t len);

#endif
