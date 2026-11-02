#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ina226.h"

/* Output groups.  Each maps to one enable GPIO and one physical DC-DC branch. */
typedef enum {
    PB_OUT_12V = 0,
    PB_OUT_24V,
    PB_OUT_USB,
    PB_OUT_COUNT,
} pb_output_t;

typedef enum {
    PB_STATE_OFF = 0,   /* no outputs active, safe after reset */
    PB_STATE_READY,     /* system healthy, outputs still off */
    PB_STATE_RUN,       /* at least one user-enabled output is active */
    PB_STATE_FAULT,     /* critical error, outputs locked off */
    PB_STATE_CHARGING,  /* external charger present, pack charging */
} pb_system_state_t;

typedef struct {
    pb_system_state_t state;
    bool out_enabled[PB_OUT_COUNT];   /* user toggle, persisted in NVS */
    bool out_on[PB_OUT_COUNT];        /* actually driven high */
    bool charger_present;
    bool charging;
    uint32_t charge_ma;               /* instantaneous charge current, mA */
} pb_output_state_t;

esp_err_t pb_outputs_init(void);

/* Called from the monitor loop.  Enforces the state machine and safety. */
void pb_outputs_update(const pb_pack_sample_t *a, const pb_pack_sample_t *b);

void pb_outputs_set_enabled(pb_output_t out, bool enabled);

bool pb_outputs_is_on(pb_output_t out);

pb_system_state_t pb_outputs_state(void);

const pb_output_state_t *pb_outputs_snapshot(void);

/* Returns the SoC percent of the block currently feeding the bus, 0..100. */
unsigned pb_outputs_active_soc_percent(void);

/* Latched-fault indicator: true while the user must be notified. */
bool pb_outputs_fault_indicate_pending(void);
void pb_outputs_ack_fault_indicate(void);

/* Acknowledge a latched fault: return to READY only while the underlying
 * condition has cleared.  Called from the UI (long press). */
void pb_outputs_ack_fault(void);