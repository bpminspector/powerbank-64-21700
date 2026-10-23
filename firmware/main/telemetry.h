#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ina226.h"

#define PB_TELEMETRY_MAGIC 0x34364250u /* bytes "PB64" on ESP32 little-endian */
#define PB_TELEMETRY_VERSION 1
#define PB_TELEMETRY_TYPE_STATUS 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t node_id;
    uint32_t sequence;
    uint32_t uptime_ms;
    uint32_t flags;
    uint32_t pack_a_mv;
    int32_t pack_a_ma;
    uint32_t pack_b_mv;
    int32_t pack_b_ma;
    uint32_t energy_a_mwh;
    uint32_t energy_b_mwh;
} pb_telemetry_packet_t;

enum {
    PB_FLAG_PACK_A_BUS_VALID = 1u << 0,
    PB_FLAG_PACK_A_CURRENT_VALID = 1u << 1,
    PB_FLAG_PACK_B_BUS_VALID = 1u << 2,
    PB_FLAG_PACK_B_CURRENT_VALID = 1u << 3,
    PB_FLAG_WIFI_CONNECTED = 1u << 4,
};

void pb_telemetry_init(void);
bool pb_telemetry_ready(void);
void pb_telemetry_send(const pb_pack_sample_t *a, const pb_pack_sample_t *b,
                       uint32_t energy_a_mwh, uint32_t energy_b_mwh,
                       bool wifi_connected);
