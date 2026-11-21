// Copyright (C) 2026 Mik027.
// SPDX-License-Identifier: GPL-3.0-only
// Runtime settings for the CPM50D emulator. Rebuild after editing.
#ifndef EMU_CONFIG_H
#define EMU_CONFIG_H

// The cabinet DLL only starts a job when the media type is 0x48 or 0x49.
#define CFG_MEDIA_BRAND      0xff
#define CFG_MEDIA_TYPE       0x48
#define CFG_CAPACITY         230
#define CFG_REMAIN_DEFAULT   200

// How long a job stays "printing" after the image has arrived.
// The game abandons a card after 60 s, so keep this well under that.
#define CFG_PRINT_MS         8000

// End an image transfer if the host goes quiet for this long.
// Covers a payload estimate that was slightly too large.
#define CFG_DRAIN_IDLE_MS    750

#endif
