#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "vehicle_data.h"
#include "Mock_CAN.h"
#include "web_files.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#define MAX_CLIENTS 4
#define CAN_TX_PIN GPIO_NUM_4
#define CAN_RX_PIN GPIO_NUM_5

// Pin WiFi/HTTP/WebSocket work to core 0; CAN tasks are pinned to core 1
// in Mock_CAN.h's can_mock_init(). Keeping these on separate cores avoids
// WiFi stack activity from jittering CAN bus timing and vice versa.
#define NET_TASK_CORE 0

extern "C" {

static const char *TAG = "DataLogger";
static httpd_handle_t server_instance = NULL;

#define LOG_BUF_SIZE 4096
static char debug_log_buf[LOG_BUF_SIZE];
static int debug_log_idx = 0;

static portMUX_TYPE log_buf_mux = portMUX_INITIALIZER_UNLOCKED;

// Funkcja przechwytująca wszystkie logi z systemu (mutex-protected -
// called from multiple tasks/cores via ESP_LOGx)
static int web_log_vprintf(const char *fmt, va_list args) {
    char temp[128];
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    if (len > 0) {
        if (len >= (int)sizeof(temp)) len = sizeof(temp) - 1; // vsnprintf truncation guard
        taskENTER_CRITICAL(&log_buf_mux);
        // Prosty bufor cykliczny - jeśli brakuje miejsca, nadpisuje od początku
        if (debug_log_idx + len >= LOG_BUF_SIZE - 1) {
            debug_log_idx = 0;
        }
        memcpy(&debug_log_buf[debug_log_idx], temp, len);
        debug_log_idx += len;
        debug_log_buf[debug_log_idx] = '\0';
        taskEXIT_CRITICAL(&log_buf_mux);
    }
    // Puszczamy logi również na fizyczny UART, żeby niczego nie zepsuć
    return vprintf(fmt, args);
}

esp_err_t log_handler(httpd_req_t *req) {
    static char snapshot[LOG_BUF_SIZE];
    taskENTER_CRITICAL(&log_buf_mux);
    memcpy(snapshot, debug_log_buf, LOG_BUF_SIZE);
    taskEXIT_CRITICAL(&log_buf_mux);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, snapshot, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t style_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, style_css, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t script_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, script_js, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t websocket_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS Handshake approved.");
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    return httpd_ws_recv_frame(req, &ws_pkt, 0);
}

static httpd_uri_t index_uri;
static httpd_uri_t style_uri;
static httpd_uri_t script_uri;
static httpd_uri_t ws_uri;
static httpd_uri_t log_uri;

static void init_uris(void) {
    index_uri.uri     = "/";          index_uri.method  = HTTP_GET; index_uri.handler  = index_handler;
    style_uri.uri     = "/style.css"; style_uri.method  = HTTP_GET; style_uri.handler  = style_handler;
    script_uri.uri    = "/script.js"; script_uri.method = HTTP_GET; script_uri.handler = script_handler;
    ws_uri.uri        = "/ws";        ws_uri.method     = HTTP_GET; ws_uri.handler     = websocket_handler;
    ws_uri.is_websocket = true;
    log_uri.uri       = "/log";       log_uri.method    = HTTP_GET; log_uri.handler    = log_handler;
}

// =====================================================================
// This task no longer generates its own fake RPM ramp / hardcoded
// coolant-oil-voltage-fuel constants. It now reads the single shared
// vehicle_data_t struct that Mock_CAN.h's rx_task() writes to whenever
// a CAN frame decodes, and just serializes whatever is currently in
// there. Once Mock_CAN.h is swapped for real CAN/K-Line acquisition,
// this task requires zero changes - it only depends on the struct.
//
// NOTE: oil temp and boost are not produced anywhere in Mock_CAN.h's
// PID set (0x0C, 0x0D, 0x05, 0x11, 0x04, 0x2F) - there's no real OBD-II
// PID being simulated for either yet, so they're omitted from the JSON
// below rather than left as silently-fake hardcoded numbers. Voltage
// is included via vehicle_data_t but will read 0.0 until a PID/decode
// path for it is added to Mock_CAN.h (see vehicle_data.h NOTE).
// =====================================================================
typedef struct {
    httpd_handle_t server;
    int            fd;
    char           payload[256];
    size_t         len;
} ws_send_ctx_t;

// This runs INSIDE the httpd task — safe to call send_frame_async here
static void ws_send_work(void *arg) {
    ws_send_ctx_t *ctx = (ws_send_ctx_t *)arg;
    if (httpd_ws_get_fd_info(ctx->server, ctx->fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
        httpd_ws_frame_t pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.payload = (uint8_t *)ctx->payload;
        pkt.len     = ctx->len;
        pkt.type    = HTTPD_WS_TYPE_TEXT;
        esp_err_t ret = httpd_ws_send_frame_async(ctx->server, ctx->fd, &pkt);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WS send failed fd=%d: %s", ctx->fd, esp_err_to_name(ret));
        }
    }
    free(ctx);
}

static void telemetry_task(void *pvParameters) {
    while (1) {
        if (server_instance != NULL) {
            vehicle_data_t data = vehicle_data_get();

            char json_string[256];
            snprintf(json_string, sizeof(json_string),
                "{\"has_data\":%s,\"rpm\":%d,\"speed\":%d,"
                "\"coolant\":%d,\"fuel\":%d,\"throttle\":%d,"
                "\"load\":%d,\"voltage\":%.1f,\"frames\":%lu}",
                data.has_data ? "true" : "false",
                data.rpm, data.speed_kmh,
                data.coolant_c, data.fuel_pct, data.throttle_pct,
                data.engine_load_pct, data.voltage,
                (unsigned long)data.frame_count);

            size_t clients = MAX_CLIENTS;
            int    client_fds[MAX_CLIENTS];

            if (httpd_get_client_list(server_instance, &clients, client_fds) == ESP_OK) {
                for (size_t i = 0; i < clients; i++) {
                    if (httpd_ws_get_fd_info(server_instance, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                        // Allocate ctx on heap — httpd_queue_work is async,
                        // stack would be gone by the time it executes
                        ws_send_ctx_t *ctx = (ws_send_ctx_t *)malloc(sizeof(ws_send_ctx_t));
                        if (ctx == NULL) { ESP_LOGE(TAG, "OOM"); continue; }
                        ctx->server = server_instance;
                        ctx->fd     = client_fds[i];
                        ctx->len    = strlen(json_string);
                        memcpy(ctx->payload, json_string, ctx->len + 1);
                        // Queue the send to run inside the httpd task context
                        esp_err_t q = httpd_queue_work(server_instance, ws_send_work, ctx);
                        if (q != ESP_OK) {
                            ESP_LOGW(TAG, "queue_work failed: %s", esp_err_to_name(q));
                            free(ctx);
                        }
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz is plenty for a dashboard
    }
}

static httpd_handle_t start_webserver(void) {
    init_uris();
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.core_id = NET_TASK_CORE; // pin httpd's internal task to core 0

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &style_uri);
        httpd_register_uri_handler(server, &script_uri);
        httpd_register_uri_handler(server, &ws_uri);
        httpd_register_uri_handler(server, &log_uri);
        return server;
    }
    return NULL;
}

static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    memcpy(wifi_config.ap.ssid,     "Car_Dashboard_IDF", strlen("Car_Dashboard_IDF"));
    memcpy(wifi_config.ap.password, "datalogger123",     strlen("datalogger123"));
    wifi_config.ap.ssid_len       = strlen("Car_Dashboard_IDF");
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode       = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

#define KLINE_UART_NUM UART_NUM_1
#define KLINE_TX_PIN GPIO_NUM_17
#define KLINE_RX_PIN GPIO_NUM_16
#define KLINE_BAUD 10400

static const char* K_TAG = "K-LINE";



static void kline_echo_test_task(void *arg) {
    ESP_LOGI(K_TAG, "--- START TESTU ECHA SPRZETOWEGO K-LINE ---");
    
    uart_config_t uart_config = {};
    uart_config.baud_rate = 10400;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity    = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_APB;
    
    uart_driver_install(KLINE_UART_NUM, 256, 256, 0, NULL, 0);
    uart_param_config(KLINE_UART_NUM, &uart_config);
    uart_set_pin(KLINE_UART_NUM, KLINE_TX_PIN, KLINE_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_flush_input(KLINE_UART_NUM);

    // Unikalna, testowa sekwencja bajtów
    uint8_t test_msg[] = {0xAA, 0xBB, 0xCC, 0xDD}; 
    
    while(1) {
        ESP_LOGI(K_TAG, "Wysylam: AA BB CC DD");
        uart_write_bytes(KLINE_UART_NUM, (const char*)test_msg, sizeof(test_msg));
        
        uint8_t echo[16] = {0};
        // Czekamy na echo 100ms
        int len = uart_read_bytes(KLINE_UART_NUM, echo, sizeof(echo), pdMS_TO_TICKS(100));
        
        if (len > 0) {
            char hex[64] = {0};
            for(int i = 0; i < len; i++) {
                char b[4]; snprintf(b, sizeof(b), "%02X ", echo[i]); strcat(hex, b);
            }
            ESP_LOGI(K_TAG, "Odebrano: %s", hex);
            
            if (len >= 4 && echo[0] == 0xAA && echo[1] == 0xBB) {
                ESP_LOGI(K_TAG, "WYNIK: Echo sprzętowe działa! Transceiver fizycznie sprawny.");
            } else {
                ESP_LOGW(K_TAG, "WYNIK: Przyszly śmieci. Sprawdź masy i zakłócenia.");
            }
        } else {
            ESP_LOGE(K_TAG, "WYNIK: BRAK ECHA! RX milczy. Sprawdź zasilanie 12V na pinie 7 układu L9637D.");
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}


static void kline_scanner_task(void *arg) {
    ESP_LOGI(K_TAG, "--- ROZPOCZYNAM SKANOWANIE MODULOW K-LINE ---");
    
    uart_config_t uart_config = {};
    uart_config.baud_rate = 10400;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity    = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_APB;
    
    uart_driver_install(KLINE_UART_NUM, 256, 256, 0, NULL, 0);
    uart_param_config(KLINE_UART_NUM, &uart_config);

    // Lista adresow do przeskanowania (Typowe dla Renault)
    uint8_t targets[] = {0x11, 0x26, 0x2C, 0x58, 0x7A, 0x33}; 
    const char* target_names[] = {"Silnik (11)", "UCH (26)", "ABS (2C)", "Airbag (58)", "Tester (7A)", "Broadcast (33)"};

    for (int i = 0; i < 6; i++) {
        uint8_t tgt = targets[i];
        
        // 1. Inicjalizacja Fast Init przed kazdym zapytaniem
        gpio_reset_pin(KLINE_TX_PIN);
        gpio_set_direction(KLINE_TX_PIN, GPIO_MODE_OUTPUT);
        gpio_set_level(KLINE_TX_PIN, 1); vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(KLINE_TX_PIN, 0); vTaskDelay(pdMS_TO_TICKS(25));
        gpio_set_level(KLINE_TX_PIN, 1); vTaskDelay(pdMS_TO_TICKS(25));
        
        // Ponowne wpiecie UART
        uart_set_pin(KLINE_UART_NUM, KLINE_TX_PIN, KLINE_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        uart_flush_input(KLINE_UART_NUM);

        // 2. Budowa ramki Start Communication z podmienionym adresem
        uint8_t chk = (0x81 + tgt + 0xF1 + 0x81) & 0xFF;
        uint8_t req[] = {0x81, tgt, 0xF1, 0x81, chk};

        ESP_LOGI(K_TAG, "Pukam do modulu: %s", target_names[i]);
        uart_write_bytes(KLINE_UART_NUM, (const char*)req, 5);

        // Wyczyszczenie echa
        uint8_t echo[5];
        uart_read_bytes(KLINE_UART_NUM, echo, 5, pdMS_TO_TICKS(50));

        // 3. Nasluch odpowiedzi
        uint8_t resp[16];
        int len = uart_read_bytes(KLINE_UART_NUM, resp, sizeof(resp), pdMS_TO_TICKS(300));

        if (len > 0) {
            char hex[64] = {0};
            for(int j = 0; j < len; j++) {
                char b[4]; snprintf(b, sizeof(b), "%02X ", resp[j]); strcat(hex, b);
            }
            ESP_LOGI(K_TAG, ">>> SUKCES! Modul %02X odpowiedzial: %s", tgt, hex);
        } else {
            ESP_LOGW(K_TAG, "Cisza od modulu %02X", tgt);
        }
        
        // Przerwa przed kolejnym testem
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    ESP_LOGI(K_TAG, "Skanowanie zakonczone.");
    while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); } // Zawieszenie zadania
}



static void real_can_task(void *arg) {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

    // Sprzętowy filtr akceptujący WYŁĄCZNIE standardowe ramki o ID 0x7E8
    // (odpowiedzi ECU na Mode 01). Bez tego kontroler wrzuca do kolejki RX
    // każdą ramkę widoczną na magistrali - w realnym aucie to setki
    // nieistotnych komunikatów innych modułów na sekundę, które zapychają
    // kolejkę i powodują, że prawdziwa odpowiedź OBD trafia do software'u
    // z opóźnieniem o jedną iterację pętli - czyli permanentny mismatch.
    twai_filter_config_t f_config = {
        .acceptance_code = (0x7E8U << 21),
        .acceptance_mask = ~(0x7FFU << 21),
        .single_filter = true
    };

    esp_err_t install_err = twai_driver_install(&g_config, &t_config, &f_config);
    esp_err_t start_err = twai_start();
    ESP_LOGI("CAN", "TWAI install=%s start=%s", esp_err_to_name(install_err), esp_err_to_name(start_err));
    if (install_err != ESP_OK || start_err != ESP_OK) {
        ESP_LOGE("CAN", "TWAI init failed - task aborting");
        vTaskDelete(NULL);
        return;
    }

    uint8_t pids_to_poll[] = { 0x0C, 0x0D, 0x05 };
    const int num_pids = sizeof(pids_to_poll) / sizeof(pids_to_poll[0]);
    int pid_idx = 0;

    while (1) {
        uint8_t current_pid = pids_to_poll[pid_idx];

        twai_message_t req_msg = {};
        req_msg.identifier = 0x7DF;
        req_msg.data_length_code = 8;
        req_msg.data[0] = 0x02;
        req_msg.data[1] = 0x01;
        req_msg.data[2] = current_pid;

        esp_err_t tx_err = twai_transmit(&req_msg, pdMS_TO_TICKS(50));
        if (tx_err != ESP_OK) {
            ESP_LOGW("CAN", "TX failed for PID 0x%02X: %s", current_pid, esp_err_to_name(tx_err));
        }

        twai_message_t rx_msg;
        esp_err_t rx_err = twai_receive(&rx_msg, pdMS_TO_TICKS(100));
        if (rx_err == ESP_OK) {
            if (rx_msg.identifier == 0x7E8 && rx_msg.data[2] == current_pid) {
                if (current_pid == 0x0C) {
                    int rpm = ((rx_msg.data[3] * 256) + rx_msg.data[4]) / 4;
                    vehicle_data_set_rpm(rpm);
                    ESP_LOGI("CAN", "RPM: %d", rpm);
                } else if (current_pid == 0x0D) {
                    int speed = rx_msg.data[3];
                    vehicle_data_set_speed(speed);
                    ESP_LOGI("CAN", "Speed: %d km/h", speed);
                } else if (current_pid == 0x05) {
                    int temp = rx_msg.data[3] - 40;
                    vehicle_data_set_coolant(temp);
                    ESP_LOGI("CAN", "Coolant: %d C", temp);
                }
            } else {
                ESP_LOGW("CAN", "Unexpected reply id=0x%X pid=0x%02X", (unsigned)rx_msg.identifier, rx_msg.data[2]);
            }
        } else if (rx_err != ESP_ERR_TIMEOUT) {
            ESP_LOGW("CAN", "RX error for PID 0x%02X: %s", current_pid, esp_err_to_name(rx_err));
        }

        pid_idx = (pid_idx + 1) % num_pids;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_log_set_vprintf(web_log_vprintf); // must be set before anything you want captured
    ESP_LOGI(TAG, "==== SYSTEM START ====");
    vehicle_data_init();
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_softap();
    server_instance = start_webserver();

    // can_mock_init() creates and pins its own tasks (tx/rx/obd_request)
    // to core 1 internally - see Mock_CAN.h. It also calls
    // vehicle_data_init(), which must happen before telemetry_task starts
    // reading from it, so this call must stay above the line below.
    //can_mock_init();
    // can_mock_init();
    xTaskCreatePinnedToCore(kline_scanner_task, "kline_task", 4096, NULL, 5, NULL, 1);
    //xTaskCreatePinnedToCore(real_can_task, "can_task", 4096, NULL, 5, NULL, 1);
    //xTaskCreatePinnedToCore(kline_echo_test_task, "kline_echo", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(telemetry_task, "telemetry", 4096, NULL, 5, NULL, NET_TASK_CORE);
}

} // extern "C"