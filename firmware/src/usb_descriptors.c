// Copyright (C) 2026 Mik027.
// SPDX-License-Identifier: GPL-3.0-only
// USB descriptors cloned from a Mitsubishi CPM50D (06d3:3b38).
// The RP2350 is Full-Speed, so bulk packets are 64 bytes (the real
// printer is High-Speed, 512). The cabinet accepts that.
#include "tusb.h"

#define EP_CMD_OUT   0x01
#define EP_RESP_IN   0x82
#define EP_DATA_OUT  0x03

// ---------------- Device descriptor ----------------
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,   // class is set on the interface
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x06D3,
    .idProduct          = 0x3B38,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,   // "Mitsubishi"
    .iProduct           = 0x02,   // "USB PRINTER"
    .iSerialNumber      = 0x00,   // no serial string
    .bNumConfigurations = 0x01,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

// ---------------- Configuration descriptor ----------------
// 9 (config) + 9 (interface) + 3*7 (endpoints) = 39, same as the real printer.
#define CONFIG_TOTAL_LEN  (9 + 9 + 7 + 7 + 7)

static const uint8_t desc_configuration[] = {
    // One interface, configuration value 1, self-powered, 100 mA.
    9, TUSB_DESC_CONFIGURATION,
    U16_TO_U8S_LE(CONFIG_TOTAL_LEN),
    0x01,                 // bNumInterfaces
    0x01,                 // bConfigurationValue
    0x00,                 // iConfiguration
    0xC0,                 // bmAttributes : Self-Powered
    50,                   // bMaxPower : 100mA

    // Printer class 7 / subclass 1 / protocol 2 (bidirectional), 3 endpoints.
    9, TUSB_DESC_INTERFACE,
    0x00,                 // bInterfaceNumber
    0x00,                 // bAlternateSetting
    0x03,                 // bNumEndpoints
    0x07,                 // bInterfaceClass  = Printer
    0x01,                 // bInterfaceSubClass
    0x02,                 // bInterfaceProtocol = bidirectional
    0x00,                 // iInterface

    // EP1 OUT bulk 64
    7, TUSB_DESC_ENDPOINT, EP_CMD_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0x00,
    // EP2 IN bulk 64
    7, TUSB_DESC_ENDPOINT, EP_RESP_IN, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0x00,
    // EP3 OUT bulk 64
    7, TUSB_DESC_ENDPOINT, EP_DATA_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0x00,
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// ---------------- String descriptors ----------------
static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: English (0x0409)
    "Mitsubishi",                // 1: iManufacturer
    "USB PRINTER",               // 2: iProduct
};

static uint16_t _desc_str[32];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;
    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
            return NULL;
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
    }
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
