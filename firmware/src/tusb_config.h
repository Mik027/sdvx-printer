// Copyright (C) 2026 Mik027.
// SPDX-License-Identifier: GPL-3.0-only

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

// CFG_TUSB_MCU is provided by the Pico SDK (RP2350).
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_PICO
#endif

#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE
#define CFG_TUD_ENABLED       1
#define CFG_TUD_ENDPOINT0_SIZE 64

// No built-in TinyUSB class. main.c registers a custom printer driver.
#define CFG_TUD_CDC    0
#define CFG_TUD_MSC    0
#define CFG_TUD_HID    0
#define CFG_TUD_MIDI   0
#define CFG_TUD_VENDOR 0

#endif // _TUSB_CONFIG_H_
