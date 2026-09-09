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
#define MAX_DTCS 10
typedef struct {
    int      rpm;
    int      speed_kmh;
    int      coolant_c;
    int      fuel_pct;
    int      throttle_pct;
    int      engine_load_pct;
    float    voltage;          // not yet decoded from CAN - see NOTE below
    int      runtime_s;  
    int protocol;      // <--- DODANE: Czas pracy silnika w sekundach
    uint32_t frame_count;      // total frames successfully decoded
    uint32_t last_update_ms;   // xTaskGetTickCount() of last write, for staleness checks
    bool     has_data;
    uint8_t dtc_count;
    char dtcs[MAX_DTCS][6];         // false until first frame decoded
    bool     freeze_frame_valid;
    int      ff_rpm;
    int      ff_load_pct;
    int      ff_coolant_c;
    bool     dtc_read_error;
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
void vehicle_data_set_runtime(int runtime_s);
void vehicle_data_set_protocol(int proto);
void vehicle_data_clear_dtcs();
void vehicle_data_add_dtc(const char* dtc_str);
void vehicle_data_inc_frame();
void vehicle_data_set_freeze_frame(int rpm, int load_pct, int coolant_c);
void vehicle_data_set_dtc_error(bool error);

#ifdef __cplusplus
}
#endif