#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// =====================================================================
// Single source of truth for live decoded vehicle parameters.
// Written by Mock_CAN.h's rx_task() whenever a CAN frame decodes.
// Read by main.cpp's telemetry task (WebSocket) and by display_update()
// (LCD). This replaces the two previously-separate mock data paths.
//
// All access goes through vehicle_data_lock()/unlock() since rx_task
// runs on one core (pinned to core 1, see Mock_CAN.h) and the WebSocket
// telemetry task runs on core 0 - this struct is the shared boundary
// between them and must be mutex-protected to avoid torn reads/writes.
// =====================================================================

typedef struct {
    int      rpm;
    int      speed_kmh;
    int      coolant_c;
    int      fuel_pct;
    int      throttle_pct;
    int      engine_load_pct;
    float    voltage;          // not yet decoded from CAN - see NOTE below
    uint32_t frame_count;      // total frames successfully decoded
    uint32_t last_update_ms;   // xTaskGetTickCount() of last write, for staleness checks
    bool     has_data;         // false until first frame decoded
} vehicle_data_t;

// NOTE: voltage isn't currently produced by any PID in Mock_CAN.h's
// tx_task/rx_task (no OBD PID for supply voltage is being simulated
// yet - real vehicles expose this via Mode 01 PID 0x42, "Control
// module voltage"). Left in the struct with a placeholder default so
// the dashboard JSON schema doesn't change again once you wire it up;
// update Mock_CAN.h's asked_pid[] and rx_task's decode switch to add it.

void vehicle_data_init(void);

// Returns a snapshot copy by value - safe, no lock held by caller after return.
vehicle_data_t vehicle_data_get(void);

// Individual field setters used by rx_task's PID decode switch.
// Each one takes the lock internally, updates frame_count/timestamp, and releases.
void vehicle_data_set_rpm(int rpm);
void vehicle_data_set_speed(int speed_kmh);
void vehicle_data_set_coolant(int coolant_c);
void vehicle_data_set_fuel(int fuel_pct);
void vehicle_data_set_throttle(int throttle_pct);
void vehicle_data_set_engine_load(int load_pct);
void vehicle_data_set_voltage(float voltage);

#ifdef __cplusplus
}
#endif