#include "vehicle_data.h"
#include "freertos/task.h"
#include <string.h>

static vehicle_data_t g_vehicle_data;
static SemaphoreHandle_t g_data_mutex = NULL;

void vehicle_data_init(void) {
    memset(&g_vehicle_data, 0, sizeof(g_vehicle_data));
    g_vehicle_data.voltage = 0.0f;
    g_data_mutex = xSemaphoreCreateMutex();
}

static void touch_locked(void) {
    g_vehicle_data.frame_count++;
    g_vehicle_data.last_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_vehicle_data.has_data = true;
}

vehicle_data_t vehicle_data_get(void) {
    vehicle_data_t copy;
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = g_vehicle_data;
        xSemaphoreGive(g_data_mutex);
    } else {
        // Lock contention/timeout - return zeroed struct rather than
        // blocking the caller indefinitely or reading a torn struct.
        memset(&copy, 0, sizeof(copy));
    }
    return copy;
}

void vehicle_data_set_rpm(int rpm) {
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_vehicle_data.rpm = rpm;
        touch_locked();
        xSemaphoreGive(g_data_mutex);
    }
}

void vehicle_data_set_speed(int speed_kmh) {
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_vehicle_data.speed_kmh = speed_kmh;
        touch_locked();
        xSemaphoreGive(g_data_mutex);
    }
}

void vehicle_data_set_coolant(int coolant_c) {
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_vehicle_data.coolant_c = coolant_c;
        touch_locked();
        xSemaphoreGive(g_data_mutex);
    }
}

void vehicle_data_set_fuel(int fuel_pct) {
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_vehicle_data.fuel_pct = fuel_pct;
        touch_locked();
        xSemaphoreGive(g_data_mutex);
    }
}

void vehicle_data_set_throttle(int throttle_pct) {
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_vehicle_data.throttle_pct = throttle_pct;
        touch_locked();
        xSemaphoreGive(g_data_mutex);
    }
}

void vehicle_data_set_engine_load(int load_pct) {
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_vehicle_data.engine_load_pct = load_pct;
        touch_locked();
        xSemaphoreGive(g_data_mutex);
    }
}

void vehicle_data_set_voltage(float voltage) {
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_vehicle_data.voltage = voltage;
        touch_locked();
        xSemaphoreGive(g_data_mutex);
    }
}