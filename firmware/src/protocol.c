// Copyright (C) 2026 Mik027.
// SPDX-License-Identifier: GPL-3.0-only

#include "protocol.h"
#include "config.h"
#include "captures.h"
#include <string.h>

// Lower deck inside a 256-byte printerstatus reply.
// Layout: mecha[2], temperature, error[3], ..., brand, type, capacity, remain.
#define DECK_LOWER_OFF 0x80
#define D_ERROR        3
#define D_MEDIA_BRAND  16
#define D_MEDIA_TYPE   17
#define D_CAPACITY     20
#define D_REMAIN       22

// job_status[0]. 0x00 means "no such job" and the DLL treats it as a
// generic error, which makes the game reprint the card.
#define JOB0_DATA  0x10
#define JOB0_PRINT 0x50
#define JOB0_END   0x80

#define MECHA_IDLE  0x00
#define MECHA_PRINT 0x20

static const uint8_t ERR_EMPTY[3] = {0x05, 0x02, 0x02};

static int      g_remain;
static uint8_t  g_ps_memory = 0x0f;  // free-bank mask; 0x0e = one bank taken
static bool     g_armed;
static uint32_t g_arm_ms;
static uint8_t  g_buf[1100];
static uint32_t g_buf_len;
static uint32_t g_drain_remaining;
static int      g_drain_channel;     // 0 = unknown, 1 = EP1, 2 = EP3
static bool     g_drain_got;
static uint32_t g_last_out_ms;
static uint16_t g_next_jobid = 1;

typedef struct {
    uint16_t id;
    bool     used;
    bool     data_done;
    bool     counted;
    uint32_t data_done_ms;
} job_t;

#define MAX_JOBS 8
static job_t g_jobs[MAX_JOBS];
static int   g_cur_job = -1;

static void on_payload_complete(void);
static void count_print(job_t *j);
static void finish_print_if_due(void);

static void release_cycle(void) {
    g_armed = false;
    g_ps_memory = 0x0f;
}

static void finish_print_if_due(void) {
    job_t *j = (g_cur_job >= 0) ? &g_jobs[g_cur_job] : NULL;
    if (j && j->used && j->data_done &&
        (plat_now_ms() - j->data_done_ms) >= (uint32_t)CFG_PRINT_MS) {
        uint32_t done_ms = j->data_done_ms;
        count_print(j);
        g_cur_job = -1;
        // Keep the job record so a later poll still answers 0x80.
        if (!g_armed || g_arm_ms <= done_ms) release_cycle();
        return;
    }
    if (j && j->used) return;
    if (!g_armed) return;
    if ((plat_now_ms() - g_arm_ms) < (uint32_t)CFG_PRINT_MS) return;
    release_cycle();
}

void proto_service(void) {
    uint32_t now = plat_now_ms();
    if (g_drain_remaining > 0 && g_drain_got &&
        (now - g_last_out_ms) >= (uint32_t)CFG_DRAIN_IDLE_MS) {
        on_payload_complete();
        g_drain_remaining = 0;
        g_drain_channel = 0;
    }
    finish_print_if_due();
}

static job_t *job_find(uint16_t id) {
    for (int i = 0; i < MAX_JOBS; i++)
        if (g_jobs[i].used && g_jobs[i].id == id) return &g_jobs[i];
    return NULL;
}

static job_t *job_alloc(uint16_t id) {
    job_t *j = job_find(id);
    if (j) return j;
    static int rr = 0;
    for (int i = 0; i < MAX_JOBS; i++) if (!g_jobs[i].used) { j = &g_jobs[i]; break; }
    if (!j) { j = &g_jobs[rr]; rr = (rr + 1) % MAX_JOBS; }
    memset(j, 0, sizeof(*j));
    j->used = true;
    j->id = id;
    return j;
}

void proto_init(void) {
    g_remain = CFG_REMAIN_DEFAULT;
    g_ps_memory = 0x0f;
    g_armed = false;
    g_arm_ms = 0;
    g_buf_len = 0;
    g_drain_remaining = 0;
    g_drain_channel = 0;
    g_drain_got = false;
    g_last_out_ms = 0;
    g_next_jobid = 1;
    g_cur_job = -1;
    memset(g_jobs, 0, sizeof(g_jobs));
}

static job_t *cur_job(void) {
    if (g_cur_job < 0) return NULL;
    job_t *j = &g_jobs[g_cur_job];
    return j->used ? j : NULL;
}

static bool job_time_done(job_t *j) {
    if (!j || !j->data_done) return false;
    return (plat_now_ms() - j->data_done_ms) >= (uint32_t)CFG_PRINT_MS;
}

// The DLL sends the image only while the mechanism reports PRINT.
// Dropping to IDLE while jobstatus is still 0x50 makes the cabinet abort.
static uint8_t job_mecha(void) {
    job_t *j = cur_job();
    if (g_drain_remaining > 0) return MECHA_PRINT;
    if (j && !job_time_done(j)) return MECHA_PRINT;
    if (g_armed && !j) return MECHA_PRINT;
    finish_print_if_due();
    return MECHA_IDLE;
}

static uint16_t u16be(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void patch_deck(uint8_t *buf, int off) {
    bool empty = (g_remain <= 0);
    buf[off + 0] = job_mecha();
    buf[off + 1] = 0x00;
    if (empty) {
        buf[off + D_ERROR + 0] = ERR_EMPTY[0];
        buf[off + D_ERROR + 1] = ERR_EMPTY[1];
        buf[off + D_ERROR + 2] = ERR_EMPTY[2];
    } else {
        buf[off + D_ERROR + 0] = 0x00;
        buf[off + D_ERROR + 1] = 0x00;
        buf[off + D_ERROR + 2] = 0x00;
    }
    buf[off + D_MEDIA_BRAND] = CFG_MEDIA_BRAND;
    buf[off + D_MEDIA_TYPE]  = CFG_MEDIA_TYPE;
    buf[off + D_CAPACITY]     = (uint8_t)((CFG_CAPACITY >> 8) & 0xff);
    buf[off + D_CAPACITY + 1] = (uint8_t)(CFG_CAPACITY & 0xff);
    int rem = g_remain < 0 ? 0 : g_remain;
    buf[off + D_REMAIN]     = (uint8_t)((rem >> 8) & 0xff);
    buf[off + D_REMAIN + 1] = (uint8_t)(rem & 0xff);
}

static void send_printerstatus(uint8_t which) {
    uint8_t resp[PRINTERSTATUS_BOTH_LEN];
    uint32_t len;
    if (which == 0x31) {
        memcpy(resp, PRINTERSTATUS_LOWER, PRINTERSTATUS_LOWER_LEN);
        len = PRINTERSTATUS_LOWER_LEN;
    } else if (which == 0x32) {
        memcpy(resp, PRINTERSTATUS_UPPER, PRINTERSTATUS_UPPER_LEN);
        len = PRINTERSTATUS_UPPER_LEN;
    } else {
        memcpy(resp, PRINTERSTATUS_BOTH, PRINTERSTATUS_BOTH_LEN);
        len = PRINTERSTATUS_BOTH_LEN;
    }
    resp[4] = g_ps_memory;
    patch_deck(resp, DECK_LOWER_OFF);
    proto_emit(resp, len);
}

static void count_print(job_t *j) {
    if (j->counted) return;
    j->counted = true;
    if (g_remain > 0) g_remain--;
}

static void send_jobstatus(uint16_t jobid) {
    uint8_t resp[JOBSTATUS_LEN];
    memcpy(resp, JOBSTATUS, JOBSTATUS_LEN);
    // Echo the requested id. Rewriting job 0 to "the current job" fails
    // the DLL's id check and the game retries forever.
    resp[4] = (uint8_t)((jobid >> 8) & 0xff);
    resp[5] = (uint8_t)(jobid & 0xff);

    uint8_t js0 = 0x00, js1 = 0x00, mecha = MECHA_IDLE;
    job_t *j = job_find(jobid);
    // After a card finishes the DLL asks for job 0. Answering 0x00 is a
    // generic error, so report the last successful job instead.
    if (!j && jobid == 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            job_t *done = &g_jobs[i];
            if (done->used && done->data_done && job_time_done(done)) {
                j = done;
                break;
            }
        }
    }
    if (j) {
        if (!j->data_done) {
            js0 = JOB0_DATA;
            mecha = MECHA_PRINT;
        } else if (!job_time_done(j)) {
            uint32_t elapsed = plat_now_ms() - j->data_done_ms;
            static const uint8_t seq[5] = {0x30, 0x50, 0x70, 0x90, 0xA0};
            uint32_t idx = (elapsed * 5) / (uint32_t)CFG_PRINT_MS;
            if (idx > 4) idx = 4;
            js0 = JOB0_PRINT;
            js1 = seq[idx];
            mecha = MECHA_PRINT;
        } else {
            js0 = JOB0_END;
            js1 = 0x00;
            mecha = MECHA_IDLE;
            finish_print_if_due();
        }
    }
    resp[8] = js0;
    resp[9] = js1;
    resp[10] = 0;
    resp[11] = 0;
    resp[12] = g_ps_memory;
    resp[14] = mecha;
    resp[15] = 0x00;
    resp[17] = 0x00;
    resp[18] = 0x00;
    resp[19] = 0x00;
    proto_emit(resp, JOBSTATUS_LEN);
}

static void send_memorystatus(void) {
    // 6 bytes: e4 56 33 <memory> <size> <rsvd>
    // size != 0 or memory == 0xff rejects the format.
    // memory != 0 means the buffers are full. 00 00 means "send the image".
    if (g_remain > 0) {
        job_t *j = cur_job();
        if (j && j->data_done && job_time_done(j)) finish_print_if_due();
        // Take one bank (0x0f -> 0x0e). The DLL waits for that before 1b 5a.
        if (!(j && !job_time_done(j)) && g_ps_memory == 0x0f) {
            g_ps_memory = 0x0e;
            g_armed = true;
            g_arm_ms = plat_now_ms();
        }
        if (j && !job_time_done(j)) {
            static const uint8_t busy[6] = {0xE4, 0x56, 0x33, 0x01, 0x00, 0x00};
            proto_emit(busy, sizeof(busy));
        } else {
            static const uint8_t ok[6] = {0xE4, 0x56, 0x33, 0x00, 0x00, 0x00};
            proto_emit(ok, sizeof(ok));
        }
        return;
    }
    static const uint8_t reject[6] = {0xE4, 0x56, 0x33, 0x00, 0x01, 0x01};
    proto_emit(reject, sizeof(reject));
}

static uint32_t pad512(uint32_t n) {
    return (n + 511u) / 512u * 512u;
}

static void start_print_job(const uint8_t *hdr) {
    uint16_t jobid = u16be(hdr + 4);
    if (jobid == 0) jobid = g_next_jobid;
    uint16_t cols = u16be(hdr + 16);
    uint16_t rows = u16be(hdr + 18);
    if (!cols) cols = 1072;
    if (!rows) rows = 712;
    uint16_t lamcols = u16be(hdr + 20);
    uint16_t lamrows = u16be(hdr + 22);
    if (!lamcols) lamcols = cols;
    if (!lamrows) lamrows = rows;
    // Lamination is bit 1 of hdr[0x29]. hdr[0x30] is the overcoat mode.
    // hdr[0x40] == 1 adds a second panel.
    uint8_t laminate = (hdr[0x29] & 0x02) ? 1 : 0;
    uint8_t second_panel = hdr[0x40] == 0x01;
    uint16_t panelcols = u16be(hdr + 0x44);
    uint16_t panelrows = u16be(hdr + 0x46);
    if (!panelcols) panelcols = cols;
    if (!panelrows) panelrows = rows;

    uint32_t pad = pad512((uint32_t)cols * rows);
    uint32_t payload = 6u * pad;
    if (laminate)
        payload += pad512((uint32_t)lamcols * lamrows * 2u);
    if (second_panel)
        payload += pad512((uint32_t)panelcols * panelrows);

    g_next_jobid = (uint16_t)((jobid % 0xFFFF) + 1);
    g_drain_remaining = payload;
    g_drain_channel = 0;
    g_drain_got = false;
    g_last_out_ms = plat_now_ms();
    g_ps_memory = 0x0e;
    job_t *j = job_alloc(jobid);
    j->data_done = false;
    j->counted = false;
    j->data_done_ms = 0;
    g_cur_job = (int)(j - g_jobs);
    if (payload == 0) {
        j->data_done = true;
        j->data_done_ms = plat_now_ms();
    }
}

static void on_payload_complete(void) {
    if (g_cur_job >= 0 && g_jobs[g_cur_job].data_done) {
        g_drain_channel = 0;
        g_drain_got = false;
        return;
    }
    g_drain_channel = 0;
    g_drain_got = false;
    if (g_cur_job >= 0) {
        g_jobs[g_cur_job].data_done = true;
        g_jobs[g_cur_job].data_done_ms = plat_now_ms();
    }
}

// Returns 0 when something was consumed, -1 when more bytes are needed.
static int try_parse_command(void) {
    uint8_t *b = g_buf;
    uint32_t n = g_buf_len;
    if (n == 0) return -1;

    if (b[0] != 0x1b) {
        uint32_t i = 0;
        while (i < n && b[i] != 0x1b) i++;
        memmove(b, b + i, n - i);
        g_buf_len = n - i;
        return (g_buf_len > 0) ? 0 : -1;
    }
    if (n < 2) return -1;
    uint8_t b1 = b[1];

    if (b1 == 0x5a) {
        if (n < 512) return -1;
        uint8_t hdr[512];
        memcpy(hdr, b, 512);
        memmove(b, b + 512, n - 512);
        g_buf_len = n - 512;
        start_print_job(hdr);
        return 0;
    }
    if (b1 == 0x56) {
        if (n < 3) return -1;
        uint8_t b2 = b[2];
        if (b2 == 0x32) {
            if (n < 4) return -1;
            uint8_t which = b[3];
            memmove(b, b + 4, n - 4);
            g_buf_len = n - 4;
            send_printerstatus(which);
            return 0;
        }
        if (b2 == 0x31) {
            if (n < 6) return -1;
            uint8_t sub = b[3];
            uint16_t jobid = u16be(b + 4);
            memmove(b, b + 6, n - 6);
            g_buf_len = n - 6;
            if (sub == 0x31) proto_emit(JOBS_ALL, JOBS_ALL_LEN);
            else send_jobstatus(jobid);
            return 0;
        }
        if (b2 == 0x33) {
            if (n < 10) return -1;
            memmove(b, b + 10, n - 10);
            g_buf_len = n - 10;
            send_memorystatus();
            return 0;
        }
        if (n < 4) return -1;
        memmove(b, b + 4, n - 4);
        g_buf_len = n - 4;
        return 0;
    }
    // cancel (1b 44), set (1b 53), keepalive (1b 45 57 55): no reply.
    if (n < 4) return -1;
    memmove(b, b + 4, n - 4);
    g_buf_len = n - 4;
    return 0;
}

// Status commands arrive on EP3 while the image arrives on EP1 (or the
// other way around). Image bytes contain 0x1b; parsing them as commands
// desynchronises the transfer and Windows resets the device.
static void proto_process(int channel, const uint8_t *data, uint32_t len) {
    while (len > 0) {
        if (g_drain_remaining > 0) {
            if (g_drain_channel == 0) g_drain_channel = channel;
            if (g_drain_channel == channel) {
                uint32_t take = (len < g_drain_remaining) ? len : g_drain_remaining;
                data += take;
                len -= take;
                g_drain_remaining -= take;
                g_drain_got = true;
                g_last_out_ms = plat_now_ms();
                if (g_drain_remaining == 0) on_payload_complete();
                continue;
            }
        }
        uint32_t space = (uint32_t)sizeof(g_buf) - g_buf_len;
        uint32_t take = (len < space) ? len : space;
        if (take == 0) { g_buf_len = 0; break; }
        memcpy(g_buf + g_buf_len, data, take);
        g_buf_len += take;
        data += take;
        len -= take;

        for (;;) {
            if (g_drain_remaining > 0 && g_drain_channel == channel) break;
            int r = try_parse_command();
            if (r < 0) break;
            if (g_buf_len == 0) break;
        }
    }
}

void proto_process_ep1(const uint8_t *data, uint32_t len) { proto_process(1, data, len); }
void proto_process_ep3(const uint8_t *data, uint32_t len) { proto_process(2, data, len); }
