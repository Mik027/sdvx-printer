# sdvx-printer

SOUND VOLTEX can print collectible cards. A Mitsubishi CPM50D does the printing. It plugs into the cabinet over USB, and the cabinet only offers the cards when it sees that printer with paper still in it.

The CPM50D does not take ordinary paper. It uses a custom roll that is no longer made. People who still have a few sell them for more than they are worth. This project is a way around that.

`sdvx-printer` is a small program for a Raspberry Pi Pico 2. Plug the Pico into the cabinet where the printer would go. The cabinet then believes a real printer is connected, and that it is loaded. Paper, ribbon, and the printer itself are not required.

Played on the cabinet, and not through a game emulator on a PC:

- With the printer plugged in and no paper, the printer lights stay red.
- With paper in the printer, or with the Pico in its place, those lights turn green.

The game then opens the GENERATOR menu. That menu is where you obtain the collectible cards. The Pico does not print a physical card. It receives the picture, throws it away, and tells the game that printing finished. You still get the whole GENERATOR feature inside the game.

## What you need

- A Raspberry Pi Pico 2, or a Pico 2 W. Only the USB port is used. On a Pico 2 W the radio stays off.
- A USB cable. The Pico is the printer. The cabinet is the computer it plugs into.
- To build the program: the [Pico SDK](https://github.com/raspberrypi/pico-sdk) and the [Arm GNU toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).

## Build

Install the Pico SDK, and make sure `arm-none-eabi-gcc` is on `PATH`. If you unpacked the toolchain into `~/pico/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin`, the build script finds it there.

```bash
export PICO_SDK_PATH=~/pico/pico-sdk
bash firmware/build.sh
```

The file to copy onto the board is `firmware/build/sdvx_printer.uf2`. It is built for a Pico 2, and the same file runs on a Pico 2 W.

`firmware/build/` is produced by the build. It is listed in `.gitignore`.

## Flash

1. Hold the BOOTSEL button and plug the Pico into the computer. Release the button.
2. A drive named `RP2350` appears.
3. Copy the UF2 onto that drive. The Pico reboots and the drive goes away.

```bash
cp firmware/build/sdvx_printer.uf2 /media/$USER/RP2350/
```

Unplug the Pico and plug it into the cabinet. The cabinet sees a Mitsubishi USB printer.

## What you can change

The values the cabinet actually notices are in `firmware/src/config.h`. Rebuild after editing them.

| Name | Default | What it changes |
|---|---|---|
| `CFG_MEDIA_BRAND` | `0xff` | Brand byte reported with the paper status |
| `CFG_MEDIA_TYPE` | `0x48` | Paper type. Leave this at `0x48` or `0x49`, or the game never sends a card |
| `CFG_CAPACITY` | `230` | How many sheets the printer claims to hold |
| `CFG_REMAIN_DEFAULT` | `200` | Sheets left after the Pico is powered on |
| `CFG_PRINT_MS` | `8000` | How long a card stays "printing" after the picture has arrived |
| `CFG_DRAIN_IDLE_MS` | `750` | How long a silent gap can be before an unfinished picture is closed anyway |

The sheet count lives in memory. Each finished card subtracts one. Power off the Pico and the count returns to 200.

## What happens for one card

The cabinet does not print the way a PC does. A program in the cabinet, `CPUSBXPKM.DLL`, sends its own commands and waits for a matching answer before it decides the card is done.

1. It asks whether the printer is ready. The Pico answers: idle, no error, the right paper, 200 sheets left out of 230. The game only starts a card when the paper type is `0x48` or `0x49`.
2. It asks whether there is room for a picture of this size. SOUND VOLTEX uses 1072×712. The Pico says yes, and marks one memory slot as busy.
3. It sends the picture. The Pico counts the bytes and ignores them. It does not read the image and it does not store it. For a normal card that is 4,580,352 bytes. A lamination layer or a second panel makes it longer. The picture itself contains the byte `0x1b`, which is also how commands start, so the Pico never looks for commands inside the picture. Doing that loses track of the transfer, and Windows resets the Pico.
4. From the first image byte until 8 seconds after the last one, the Pico says it is printing. The game gives up on a card after 60 seconds. Eight seconds leaves room for about 4.5 MB to arrive over USB. Saying "idle" while the card is still "printing" makes the cabinet cancel it.
5. It asks about that card by its job number. The Pico repeats the same number and, once the 8 seconds are over, says finished with success. It remembers the last 8 cards, so a later question still gets "finished".
6. A keepalive, a cancel, and a settings write get no answer. That is what the real printer does.

If the host goes quiet for 750 ms before the expected number of bytes has arrived, the Pico closes the transfer anyway. That covers a size estimate that was a little high.

You can stop here. The next sections are for anyone who wants to change how the Pico answers.

## What the cabinet sees on USB

The USB name is copied from a real CPM50D.

| Field | Value |
|---|---|
| Vendor / product | `06d3` / `3b38` |
| Manufacturer | `Mitsubishi` |
| Product | `USB PRINTER` |
| Serial string | empty |
| Interface | printer class 7, subclass 1, protocol 2 (bidirectional) |
| Endpoints | 3 bulk pipes, 64-byte packets |

The real printer is USB High-Speed, so its packets are 512 bytes. The Pico is Full-Speed, so they are 64. The cabinet accepts that.

| Endpoint | Direction | Role |
|---|---|---|
| `0x01` | OUT | Commands, and the picture when it follows the header on this pipe |
| `0x82` | IN | Every reply |
| `0x03` | OUT | Status commands, and the picture when it arrives on this pipe instead |

Three standard printer requests also arrive on the control pipe:

- `GET_DEVICE_ID` returns the IEEE-1284 string. The model inside it is `CPM50D`.
- `GET_PORT_STATUS` returns `0x18`: selected, no error, paper present.
- `SOFT_RESET` is acknowledged and does nothing else.

## Command reference

### Printer status

The cabinet asks `1b 56 32 30`. The reply is 256 bytes and starts with `e4 56 32 30`.

The useful block sits at offset `0x80` inside that reply:

- mechanism: `0x00` idle, or `0x20` while a card is in progress
- three error bytes: cleared while sheets remain, `05 02 02` when the count hits zero
- media brand `0xff`, media type `0x48`
- capacity `230`, sheets remaining, big-endian, starting at `200`

Byte 4 of the same reply is a free-bank mask. `0x0f` means every bank is free. `0x0e` means one bank is taken.

The captured reply also contains `0x80` bytes outside those fields. They belong to the real printer's status. Leave them. The game reads errors from the three bytes at deck offset 3, and those are the ones the program clears.

A shorter form of the same status exists (`1b 56 32 31`, 192 bytes). The program answers both.

### Memory status

The cabinet asks `1b 56 33` followed by the card size. The reply is 6 bytes.

`e4 56 33 00 00 00` means the size is accepted and a buffer is free, so the cabinet may send the picture. On that answer the program takes one bank (`0x0f` becomes `0x0e`) and arms a job.

While a job is still running, the same query returns memory `0x01`: buffers busy.

A non-zero size byte, or memory `0xff`, means the format was rejected. When the sheet count has reached 0, the reply is `e4 56 33 00 01 01` and the cabinet stops.

### The picture

The cabinet sends `1b 5a 54` and a 512-byte header. The job id is a big-endian 16-bit value at offset 4. Columns and rows are at offsets 16 and 18. The pixel bytes follow on the same endpoint.

The program computes how many bytes belong to this card and ignores that many bytes on that endpoint. Status polls keep being answered on the other endpoint while the picture is coming in.

For 1072×712, with no lamination and no second panel, the payload is 4,580,352 bytes: six passes of `cols × rows`, each pass rounded up to a multiple of 512.

Two header flags change that length:

- Lamination is bit 1 of byte `0x29` (`header[0x29] & 0x02`). It adds one plane of `lam_cols × lam_rows × 2`, also rounded up to 512. Byte `0x30` is the overcoat mode, not this flag.
- Byte `0x40` equal to `1` adds a second panel of `panel_cols × panel_rows`, rounded up to 512.

### While it is printing

From the first image byte until 8 seconds after the last one, printer status reports the mechanism as printing (`0x20`). Job status in that window is `0x50`.

The cabinet keeps waiting on job status `0x10`, `0x20`, and `0x50`. It stops on `0x00` and on `0x80`.

The mechanism returns to idle at the same moment the job becomes `0x80`. Reporting idle while the job is still `0x50` makes the cabinet abort the card.

### Job status

The cabinet asks `1b 56 31 30` plus the job id (6 bytes). The reply is 26 bytes.

The id in the reply is the id that was asked for. The cabinet compares the two. A mismatch fails the print.

The first status byte:

| Value | Meaning for the cabinet |
|---|---|
| `0x10` | The picture has not finished arriving. Keep waiting. |
| `0x50` | Printing. The next byte walks `0x30`, `0x50`, `0x70`, `0x90`, `0xA0` across the 8 seconds. Keep waiting. |
| `0x80` | Finished. The next byte `0x00` means success. Stop. |
| `0x00` | No such job. The cabinet turns this into a generic error and the game sends the card again. |

A finished job stays `0x80`. When a job id is reused, that record starts over.

After a success the cabinet asks for job 0. Answering `0x00` there is the generic error, so job 0 is answered with the finished status, and the echoed id is still 0.

A query of every job (`1b 56 31 31`) returns a fixed 1024-byte block.

## Files

| File | What it does |
|---|---|
| `firmware/src/main.c` | USB printer, the three endpoints, the class requests |
| `firmware/src/usb_descriptors.c` | Name, vendor id, product id, packet size |
| `firmware/src/protocol.c` | Commands, picture length, card timing |
| `firmware/src/protocol.h` | The functions `main.c` calls |
| `firmware/src/captures.h` | Status replies captured from a real CPM50D. `protocol.c` patches paper, errors, and the busy byte before sending them |
| `firmware/src/config.h` | Paper type, sheet count, print duration |
| `firmware/src/tusb_config.h` | TinyUSB device mode. No serial, no mass storage, no HID |
| `firmware/CMakeLists.txt` | Pico SDK build, board `pico2` |
| `firmware/pico_sdk_import.cmake` | Helper copied from the Pico SDK. It stays under the SDK's license |
| `firmware/build.sh` | Configure and compile |

## License

Copyright (C) 2026 Mik027.

The program is released under the GNU General Public License v3.0 only. The full text is in [`LICENSE`](LICENSE).

You can use it, study it, and change it. If you distribute the program or a modified version, you keep this license and you make the corresponding source available. There is no warranty.

`firmware/pico_sdk_import.cmake` is copied from the Raspberry Pi Pico SDK and remains under that SDK's license.
