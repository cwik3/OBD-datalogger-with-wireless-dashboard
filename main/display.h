// display.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// No physical display attached - vehicle data goes out over the
// WebSocket (see main.cpp's telemetry_task + script.js) instead of an
// LCD. This is a no-op stub so Mock_CAN.h's rx_task() still has
// something to call without needing real display driver code.
static inline void display_update(int rpm, int fuel_pct, int speed_kmh,
                                   int coolant_c, int throttle_pct,
                                   int engine_load_pct)
{
    (void)rpm;
    (void)fuel_pct;
    (void)speed_kmh;
    (void)coolant_c;
    (void)throttle_pct;
    (void)engine_load_pct;
}

#ifdef __cplusplus
}
#endif