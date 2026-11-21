# cpm50d-emu

SOUND VOLTEX prints a player card on a Mitsubishi CPM50D. That printer is a USB dye-sublimation printer. The cabinet does not use a normal print spooler. `CPUSBXPKM.DLL` sends a private command set, then waits for a matching status reply before it decides the card is finished.

`cpm50d-emu` is firmware for a Raspberry Pi Pico 2. Plugged into the cabinet, the Pico shows up as that printer. The cabinet sends the card. The Pico counts the bytes, drops the image, and answers the status the DLL is waiting for. The game then leaves the printing screen. Paper, ribbon, and a physical printer are not required.

The image is discarded. The Pico never decodes it and never stores it.

## What you need

- A Raspberry Pi Pico 2, or a Pico 2 W. Only the USB port is used. On a Pico 2 W the radio stays off.
- A USB cable. The Pico is the device. The cabinet is the host.
- To build: the [Pico SDK](https://github.com/raspberrypi/pico-sdk) and the [Arm GNU toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).

## What the cabinet sees

The USB identity is copied from a real CPM50D.

| Field | Value |
|---|---|
| Vendor / product | `06d3` / `3b38` |
| Manufacturer | `Mitsubishi` |
| Product | `USB PRINTER` |
| Serial string | empty |
| Interface | printer class 7, subclass 1, protocol 2 (bidirectional) |
| Endpoints | 3 bulk pipes, 64-byte packets |

The real printer is USB High-Speed, so its bulk packets are 512 bytes. The Pico is Full-Speed, so they are 64. The cabinet accepts that.

| Endpoint | Direction | Role |
|---|---|---|
| `0x01` | OUT | Commands, and the image when it follows the header on this pipe |
| `0x82` | IN | Every reply |
| `0x03` | OUT | Status commands, and the image when it arrives on this pipe instead |

The DLL also sends three printer-class requests on the control pipe:

- `GET_DEVICE_ID` returns the IEEE-1284 string. The model inside it is `CPM50D`.
- `GET_PORT_STATUS` returns `0x18`: selected, no error, paper present.
- `SOFT_RESET` is acknowledged and does nothing else.

## One card, from the first poll to the end

### 1. Printer status

The DLL asks `1b 56 32 30`. The reply is 256 bytes and starts with `e4 56 32 30`.

The useful block sits at offset `0x80` inside that reply:

- mechanism: `0x00` idle, or `0x20` while a card is in progress
- three error bytes: cleared while sheets remain, `05 02 02` when the count hits zero
- media brand `0xff`, media type `0x48`
- capacity `230`, sheets remaining, big-endian, starting at `200`

Byte 4 of the same reply is a free-bank mask. `0x0f` means every bank is free. `0x0e` means one bank is taken.

The DLL starts a job only when the media type is `0x48` or `0x49`. Any other type and it never sends the card.

The captured reply also contains `0x80` bytes outside those fields. They belong to the real printer's status. Leave them. The game reads errors from the three bytes at deck offset 3, and those are the ones the firmware clears.

A shorter form of the same status exists (`1b 56 32 31`, 192 bytes). The firmware answers both.

### 2. Memory status

The DLL asks `1b 56 33` followed by the card size. The game uses 1072×712. The reply is 6 bytes.

`e4 56 33 00 00 00` means the size is accepted and a buffer is free, so the DLL may send the image. On that answer the firmware takes one bank (`0x0f` becomes `0x0e`) and arms a job.

While a job is still running, the same query returns memory `0x01`: buffers busy.

A non-zero size byte, or memory `0xff`, means the format was rejected. When the sheet count has reached 0, the reply is `e4 56 33 00 01 01` and the DLL stops.

The sheet count lives in RAM. Each finished card subtracts one. A power cycle restores it to 200.

### 3. The image

The DLL sends `1b 5a 54` and a 512-byte header. The job id is a big-endian 16-bit value at offset 4. Columns and rows are at offsets 16 and 18. The pixel bytes follow on the same endpoint.

The firmware computes how many bytes belong to this card and ignores that many bytes on that endpoint. Status polls keep being answered on the other endpoint while the image is coming in.

For 1072×712, with no lamination and no second panel, the payload is 4,580,352 bytes: six passes of `cols × rows`, each pass rounded up to a multiple of 512.

Two header flags change that length:

- Lamination is bit 1 of byte `0x29` (`header[0x29] & 0x02`). It adds one plane of `lam_cols × lam_rows × 2`, also rounded up to 512. Byte `0x30` is the overcoat mode, not this flag.
- Byte `0x40` equal to `1` adds a second panel of `panel_cols × panel_rows`, rounded up to 512.

The image contains the byte `0x1b`. That same byte starts every command. The firmware does not scan the image channel for commands. Scanning it loses sync, and Windows resets the Pico.

If the host goes quiet for 750 ms before the expected count is reached, the transfer is closed anyway. That covers a size estimate that was a little high.

### 4. While it is "printing"

From the first image byte until 8 seconds after the last one, printer status reports the mechanism as printing (`0x20`). Job status in that window is `0x50`.

The DLL keeps waiting on job status `0x10`, `0x20`, and `0x50`. It stops on `0x00` and on `0x80`.

The mechanism returns to idle at the same moment the job becomes `0x80`. Reporting idle while the job is still `0x50` makes the cabinet abort the card.

The game abandons a card after 60 seconds. Eight seconds of simulated printing leaves room for the USB transfer of about 4.5 MB at Full-Speed.

### 5. Job status

The DLL asks `1b 56 31 30` plus the job id (6 bytes). The reply is 26 bytes.

The id in the reply is the id that was asked for. The DLL compares the two. A mismatch fails the print.

The first status byte:

| Value | Meaning for the DLL |
|---|---|
| `0x10` | The image has not finished arriving. Keep waiting. |
| `0x50` | Printing. The next byte walks `0x30`, `0x50`, `0x70`, `0x90`, `0xA0` across the 8 seconds. Keep waiting. |
| `0x80` | Finished. The next byte `0x00` means success. Stop. |
| `0x00` | No such job. The DLL turns this into a generic error and the game sends the card again. |

A finished job stays `0x80`. The firmware remembers up to 8 jobs so a later poll still says finished. When a job id is reused, that record starts over.

After a success the DLL asks for job 0. Answering `0x00` there is the generic error, so job 0 is answered with the finished status, and the echoed id is still 0.

### 6. Commands with no reply

- Keepalive `1b 45 57 55`
- Cancel `1b 44 …`
- Set `1b 53 …`

A query of every job (`1b 56 31 31`) returns a fixed 1024-byte block.

## Layout

| File | What it does |
|---|---|
| `firmware/src/main.c` | USB printer class, the three endpoints, the class requests |
| `firmware/src/usb_descriptors.c` | Name, vendor id, product id, packet size |
| `firmware/src/protocol.c` | Commands, image length, job timing |
| `firmware/src/protocol.h` | The functions `main.c` calls |
| `firmware/src/captures.h` | Status replies captured from a real CPM50D. `protocol.c` patches media, errors, and the mechanism byte before sending them |
| `firmware/src/config.h` | Media type, sheet count, print duration |
| `firmware/src/tusb_config.h` | TinyUSB device mode. No serial, no mass storage, no HID |
| `firmware/CMakeLists.txt` | Pico SDK build, board `pico2` |
| `firmware/pico_sdk_import.cmake` | Helper copied from the Pico SDK. It stays under the SDK's license |
| `firmware/build.sh` | Configure and compile |

Comments in the sources are in English.

## Build

Install the Pico SDK and put the Arm GNU toolchain on `PATH`. `arm-none-eabi-gcc` has to be visible. `build.sh` also looks in `~/pico/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin` if that directory exists.

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
bash firmware/build.sh
```

The image is `firmware/build/cpm50d_emu.uf2`. The board is `pico2`. The same image runs on a Pico 2 W.

`firmware/build/` is generated. It is listed in `.gitignore`.

## Flash

1. Hold the BOOTSEL button and plug the Pico into the computer. Release the button.
2. A drive named `RP2350` appears.
3. Copy the UF2 onto it. The Pico reboots and leaves that drive.

```bash
cp firmware/build/cpm50d_emu.uf2 /media/$USER/RP2350/
```

Unplug the Pico and plug it into the cabinet. The cabinet sees `Mitsubishi USB PRINTER`.

## Settings

`firmware/src/config.h` holds the values the cabinet actually notices. Rebuild after changing them.

| Name | Default | Role |
|---|---|---|
| `CFG_MEDIA_BRAND` | `0xff` | Brand byte in the status deck |
| `CFG_MEDIA_TYPE` | `0x48` | Must stay `0x48` or `0x49` |
| `CFG_CAPACITY` | `230` | Sheet capacity reported to the DLL |
| `CFG_REMAIN_DEFAULT` | `200` | Sheets left after power-on |
| `CFG_PRINT_MS` | `8000` | How long a job stays "printing" after the image has arrived |
| `CFG_DRAIN_IDLE_MS` | `750` | Quiet gap that closes an image transfer early |

## License

Copyright (C) 2026 Mik027.

The firmware is released under the GNU General Public License v3.0 only. The full text is in [`LICENSE`](LICENSE).

You can use it, study it, and change it. If you distribute the firmware or a modified version, you keep this license and you make the corresponding source available. There is no warranty.

`firmware/pico_sdk_import.cmake` is copied from the Raspberry Pi Pico SDK and remains under that SDK's license.
