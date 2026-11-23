// Copyright (C) 2026 Mik027.
// SPDX-License-Identifier: GPL-3.0-only
// sdvx-printer: the cabinet sees a loaded Mitsubishi CPM50D (06d3:3b38).
#include "pico/stdlib.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#include <string.h>

#include "protocol.h"
#include "captures.h"

#define EP_CMD_OUT   0x01
#define EP_RESP_IN   0x82
#define EP_DATA_OUT  0x03

uint32_t plat_now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static uint8_t g_rhport;
static uint8_t out1_buf[64];
static uint8_t out3_buf[64];
static uint8_t resp_buf[PROTO_RESP_MAX];

void proto_emit(const uint8_t *data, uint32_t len) {
    if (len > sizeof(resp_buf)) len = sizeof(resp_buf);
    memcpy(resp_buf, data, len);
    usbd_edpt_xfer(g_rhport, EP_RESP_IN, resp_buf, (uint16_t)len);
}

static void printer_init(void) {}
static void printer_reset(uint8_t rhport) { (void)rhport; }

static uint16_t printer_open(uint8_t rhport, tusb_desc_interface_t const *desc_itf,
                             uint16_t max_len) {
    TU_VERIFY(desc_itf->bInterfaceClass == 0x07, 0);

    g_rhport = rhport;
    uint16_t drv_len = desc_itf->bLength;
    uint8_t const *desc = (uint8_t const *)desc_itf;
    desc = tu_desc_next(desc);

    for (int i = 0; i < desc_itf->bNumEndpoints; i++) {
        if (tu_desc_type(desc) == TUSB_DESC_ENDPOINT) {
            tusb_desc_endpoint_t const *epd = (tusb_desc_endpoint_t const *)desc;
            usbd_edpt_open(rhport, epd);
        }
        drv_len += tu_desc_len(desc);
        desc = tu_desc_next(desc);
    }
    TU_VERIFY(drv_len <= max_len, 0);

    usbd_edpt_xfer(rhport, EP_CMD_OUT, out1_buf, sizeof(out1_buf));
    usbd_edpt_xfer(rhport, EP_DATA_OUT, out3_buf, sizeof(out3_buf));
    return drv_len;
}

// Select + NoError. Paper is present.
static uint8_t g_port_status = 0x18;

static bool printer_control_xfer(uint8_t rhport, uint8_t stage,
                                 tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) return true;

    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_CLASS) {
        switch (request->bRequest) {
        case 0x00: {  // GET_DEVICE_ID
            uint16_t len = request->wLength < DEVICE_ID_LEN ? request->wLength
                                                            : DEVICE_ID_LEN;
            return tud_control_xfer(rhport, request, (void *)DEVICE_ID, len);
        }
        case 0x01:    // GET_PORT_STATUS
            return tud_control_xfer(rhport, request, &g_port_status, 1);
        case 0x02:    // SOFT_RESET
            return tud_control_status(rhport, request);
        default:
            return false;
        }
    }
    return false;
}

static bool printer_xfer(uint8_t rhport, uint8_t ep_addr, xfer_result_t result,
                         uint32_t xferred_bytes) {
    (void)result;
    if (ep_addr == EP_CMD_OUT) {
        proto_process_ep1(out1_buf, xferred_bytes);
        usbd_edpt_xfer(rhport, EP_CMD_OUT, out1_buf, sizeof(out1_buf));
    } else if (ep_addr == EP_DATA_OUT) {
        proto_process_ep3(out3_buf, xferred_bytes);
        usbd_edpt_xfer(rhport, EP_DATA_OUT, out3_buf, sizeof(out3_buf));
    }
    return true;
}

static const usbd_class_driver_t printer_driver = {
    .name            = "CPM50D",
    .init            = printer_init,
    .reset           = printer_reset,
    .open            = printer_open,
    .control_xfer_cb = printer_control_xfer,
    .xfer_cb         = printer_xfer,
    .sof             = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &printer_driver;
}

int main(void) {
    proto_init();
    tusb_init();
    while (true) {
        tud_task();
        proto_service();
    }
    return 0;
}
