#pragma once

#include "vehicle_data.h"
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "display.h"
#include <math.h>

static const char *MOCK_CAN_TAG = "TWAI/CAN_MOCK_test";   // FIX: renamed from TAG (collided with main.cpp's TAG)

// global handle for the TWAI node
static twai_node_handle_t node_hdl = NULL;
static QueueHandle_t rx_queue = NULL;

static int current_rpm = 0;

#define CAN_TASK_CORE 1

typedef struct{
    uint32_t id;
    uint8_t data[8];
    uint8_t dlc;
} can_message_t;

int can_mock_get_rpm(void) {
    return current_rpm;
}

static void tx_task(void *arg){
    ESP_LOGI(MOCK_CAN_TAG, "Starting TX task");
    int current_rpm = 800;
    int current_speed = 10;
    int add = 10;
    int inc = 50;
    int state = 0;
    int current_fuel_value = 100;
    int fuel_dec = -1;
    static int step = 0;
    static const int MAX_STEPS = 100;
    int engine_load = 0;

    const TickType_t fuel_decrease_interval = pdMS_TO_TICKS(1500);
    TickType_t last_fuel_decrease_time = xTaskGetTickCount();
    uint8_t data_payload1[8] = {0x04, 0x41, 0x00, 0x00, 0x00, 0xAA, 0xAA, 0xAA};

    // FIX: nested designated initializers (.header.id = ...) are not
    // valid C++ - struct must be nested explicitly.
    twai_frame_t message = {0};
    message.header.id  = 0x7E8;
    message.header.ide = false;
    message.header.rtr = false;
    message.header.dlc = 8;
    message.buffer     = data_payload1;
    message.buffer_len = 8;

    while (1) {
        if(state == 0){
            data_payload1[2] = 0x0C;
            int rpm_value = current_rpm * 4;
            data_payload1[3] = (rpm_value >> 8) & 0xFF;
            data_payload1[4] = rpm_value & 0xFF;

            esp_err_t ret = twai_node_transmit(node_hdl, &message, pdMS_TO_TICKS(100));
            if (ret != ESP_OK) ESP_LOGE(MOCK_CAN_TAG, "TX Error RPM: %s", esp_err_to_name(ret));
            current_rpm += inc;
            if (current_rpm > 3000 || current_rpm < 800) {
                inc = -inc;
            }
            state = 1;
        } else if (state == 1){
            data_payload1[2] = 0x2F;
            TickType_t current_time = xTaskGetTickCount();
            if(current_time - last_fuel_decrease_time >= fuel_decrease_interval){
                if(current_fuel_value > 0){
                    current_fuel_value += fuel_dec;
                }
                last_fuel_decrease_time = current_time;
            }
            if (current_fuel_value <= 0) {
                current_fuel_value = 0;
                ESP_LOGE(MOCK_CAN_TAG, "Fuel level is empty Carr is stopped");
            }
            data_payload1[3] = (current_fuel_value * 255) / 100;
            esp_err_t ret = twai_node_transmit(node_hdl, &message, pdMS_TO_TICKS(100));
            if (ret != ESP_OK) {
                ESP_LOGE(MOCK_CAN_TAG, "Error w Ramce CANu w fuel status: %s", esp_err_to_name(ret));
            }
            state = 2;
        } else if (state == 2){
            data_payload1[2] = 0x0D;
            data_payload1[3] = current_speed;
            esp_err_t ret = twai_node_transmit(node_hdl, &message, pdMS_TO_TICKS(100));
            if (ret != ESP_OK) {
                ESP_LOGE(MOCK_CAN_TAG, "TX Error w ramce vehicle speed: %s", esp_err_to_name(ret));
            }
            current_speed += add;
            if (current_speed >= 150) {
                add = -15;
            } else if (current_speed <= 30) {
                add = 35;
            }
            state = 3;
        }else if(state == 3){
            data_payload1[2] = 0x05;
            step++;
            if (step > MAX_STEPS) {
                step = MAX_STEPS;
            }
            int current_coolant = 20 + (int)(70.0f * (log(1.0f + step) / log(1.0f + MAX_STEPS)));
            data_payload1[3] = current_coolant + 40;
            twai_node_transmit(node_hdl, &message, pdMS_TO_TICKS(100));
            state = 4;
        }else if(state == 4){
            data_payload1[2] = 0x11;
            static float angle = 0.0f;
            angle += 0.2f;
            if (angle > 2.0f * 3.14159f) {
                angle -= 2.0f * 3.14159f;
            }
            int throttle_pos = (int)(50.0f + 50.0f * sin(angle));
            data_payload1[3] = (throttle_pos * 255) / 100;

            twai_node_transmit(node_hdl, &message, pdMS_TO_TICKS(100));
            state = 5;
        } else if(state == 5){
            data_payload1[2] = 0x04;
            data_payload1[3] = (engine_load * 255) / 100;
            esp_err_t ret = twai_node_transmit(node_hdl, &message, pdMS_TO_TICKS(100));
            if (ret != ESP_OK) {
                ESP_LOGE(MOCK_CAN_TAG, "TX Error w ramce engine_load: %s", esp_err_to_name(ret));
            }
            engine_load += 2;
            if (engine_load > 100) engine_load = 0;

            state = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static bool rx_callback(twai_node_handle_t node,const twai_rx_done_event_data_t *event_data, void *user_data){
    uint8_t isr_buffer[8];
    // FIX: -Werror=missing-field-initializers - zero-init header explicitly
    twai_frame_t rx_message = {
        .header = {0},
        .buffer = isr_buffer,
        .buffer_len = sizeof(isr_buffer)
    };
    if(twai_node_receive_from_isr(node, &rx_message) == ESP_OK){

        can_message_t safe_msg;
        safe_msg.id = rx_message.header.id;
        safe_msg.dlc = rx_message.header.dlc;

        for (int i = 0; i < rx_message.header.dlc; i++) {
            safe_msg.data[i] = isr_buffer[i];
        }
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(rx_queue, &safe_msg, &xHigherPriorityTaskWoken);
        return xHigherPriorityTaskWoken == pdTRUE;
    }
    return false;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static void obd_request_task(uint8_t pid){
    static uint8_t request_data[8] = {0x02, 0x01, 0x00, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    request_data[2] = pid;
    // FIX: nested designated initializer
    static twai_frame_t request_frame = {0};
    request_frame.header.id  = 0x7DF;
    request_frame.header.ide = false;
    request_frame.header.rtr = false;
    request_frame.header.dlc = 8;
    request_frame.buffer     = request_data;
    request_frame.buffer_len = 8;

    esp_err_t ret = twai_node_transmit(node_hdl, &request_frame, pdMS_TO_TICKS(10));
    if (ret != ESP_OK) {
        ESP_LOGE("OBD_REQ", "Frame request failed for PID: 0x%02X", pid);
    }
}
#pragma GCC diagnostic pop

static void obd_request_recieve_task(void *arg){
    uint8_t asked_pid[] = {0x0C, 0x2F, 0x0D, 0x05, 0x11, 0x04};
    int num_pids = sizeof(asked_pid) / sizeof(asked_pid[0]);
    int current_pid_index = 0;
    while(1){
        obd_request_task(asked_pid[current_pid_index]);
        current_pid_index ++;
        if(current_pid_index >= num_pids){
            current_pid_index = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void rx_task(void *arg)
{
    ESP_LOGI(MOCK_CAN_TAG, "starting RX task");
    can_message_t received_msg;
    int rx_fuel_level = 0;
    int rx_speed = 0;
    int rx_coolant_temp = 0;
    int rx_throttle_pos = 0;
    int rx_engine_load = 0;

    TickType_t last_display_update = xTaskGetTickCount();
    const TickType_t display_interval = pdMS_TO_TICKS(200);

    while (1) {
        if (xQueueReceive(rx_queue, &received_msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            ESP_LOGI("RX", "Ramka otzrymana ID: 0x%lX", received_msg.id);
            if (received_msg.id == 0x7E8) {
                if(received_msg.data[2] == 0x0C){
                    current_rpm = ((received_msg.data[3] * 256) + received_msg.data[4]) / 4;
                    vehicle_data_set_rpm(current_rpm);
                }else if(received_msg.data[2] == 0x2F){
                    rx_fuel_level = (received_msg.data[3] * 100)/ 255;
                    vehicle_data_set_fuel(rx_fuel_level);
                } else if(received_msg.data[2] == 0x0D){
                    rx_speed = received_msg.data[3];
                    vehicle_data_set_speed(rx_speed);
                } else if(received_msg.data[2] == 0x05){
                    rx_coolant_temp = received_msg.data[3] - 40;
                    vehicle_data_set_coolant(rx_coolant_temp);
                }else if(received_msg.data[2] == 0x11){
                    rx_throttle_pos = (received_msg.data[3] * 100) / 255;
                    vehicle_data_set_throttle(rx_throttle_pos);
                }else if(received_msg.data[2] == 0x04){
                    rx_engine_load = (received_msg.data[3] * 100) / 255;
                    vehicle_data_set_engine_load(rx_engine_load);
                }
                ESP_LOGI(MOCK_CAN_TAG, "Ramka CAN z Paramterami Odebarana %d, Fuel Level: %d%%, Speed: %d KM/H, Coolant Temp: %d, Throttle Pos: %d, Engine Load: %d", current_rpm, rx_fuel_level, rx_speed, rx_coolant_temp, rx_throttle_pos, rx_engine_load);

                TickType_t now = xTaskGetTickCount();
                if (now - last_display_update >= display_interval) {
                    display_update(current_rpm, rx_fuel_level, rx_speed, rx_coolant_temp, rx_throttle_pos, rx_engine_load);
                    last_display_update = now;
                }
            }
        }
    }
}

void can_mock_init(void)
{
    ESP_LOGI(MOCK_CAN_TAG, "Initializing TWAI");
    vehicle_data_init();
    rx_queue = xQueueCreate(10, sizeof(can_message_t));

    // FIX: nested designated initializers
    twai_onchip_node_config_t node_config = {
        .io_cfg = {
            .tx = (gpio_num_t)5,
            .rx = (gpio_num_t)4,
        },
        .bit_timing = {
            .bitrate = 500000,
        },
        .tx_queue_depth = 5,
        .flags = {
            .enable_self_test = true,
            .enable_loopback  = true,
        }
    };

    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_hdl));
    ESP_LOGI(MOCK_CAN_TAG, "TWAI node created");

    // FIX: missing-field-initializers - zero remaining anon-struct members
    twai_mask_filter_config_t filter_cfg = {
        .id = 0x7E8,
        .mask = 0x7F8,
        .is_ext = false,
    };
    ESP_ERROR_CHECK(twai_node_config_mask_filter(node_hdl, 0, &filter_cfg));

    twai_event_callbacks_t cbs = {0};   // FIX: zero-init first, then set the one field
    cbs.on_rx_done = rx_callback;
    ESP_ERROR_CHECK(twai_node_register_event_callbacks(node_hdl, &cbs, NULL));

    ESP_ERROR_CHECK(twai_node_enable(node_hdl));
    ESP_LOGI(MOCK_CAN_TAG, "TWAI Node enabled and running");

    xTaskCreatePinnedToCore(tx_task, "twai_tx_task", 4096, NULL, 5, NULL, CAN_TASK_CORE);
    xTaskCreatePinnedToCore(obd_request_recieve_task, "obd_request_task", 4096, NULL, 5, NULL, CAN_TASK_CORE);
    xTaskCreatePinnedToCore(rx_task, "twai_rx_task", 4096, NULL, 5, NULL, CAN_TASK_CORE);
}