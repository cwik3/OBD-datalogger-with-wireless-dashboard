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

// --- KONFIGURACJA HARDWARE ---
#define CAN_TX_PIN GPIO_NUM_4
#define CAN_RX_PIN GPIO_NUM_5

#define KLINE_UART_NUM UART_NUM_1
#define KLINE_TX_PIN GPIO_NUM_17
#define KLINE_RX_PIN GPIO_NUM_16
#define KLINE_BAUD 10400

#define MAX_CLIENTS 4
#define NET_TASK_CORE 0

extern "C" {

static const char *TAG = "DataLogger";
static const char *GATEWAY_TAG = "GATEWAY";
static httpd_handle_t server_instance = NULL;

#define LOG_BUF_SIZE 4096
static char debug_log_buf[LOG_BUF_SIZE];
static int debug_log_idx = 0;
static portMUX_TYPE log_buf_mux = portMUX_INITIALIZER_UNLOCKED;

// Funkcja przechwytująca wszystkie logi z systemu do podglądu przez przeglądarkę
static int web_log_vprintf(const char *fmt, va_list args) {
    char temp[128];
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    if (len > 0) {
        if (len >= (int)sizeof(temp)) len = sizeof(temp) - 1;
        taskENTER_CRITICAL(&log_buf_mux);
        if (debug_log_idx + len >= LOG_BUF_SIZE - 1) {
            debug_log_idx = 0;
        }
        memcpy(&debug_log_buf[debug_log_idx], temp, len);
        debug_log_idx += len;
        debug_log_buf[debug_log_idx] = '\0';
        taskEXIT_CRITICAL(&log_buf_mux);
    }
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

volatile bool g_req_read_dtc = false;
volatile bool g_req_clear_dtc = false;

static esp_err_t websocket_handler(httpd_req_t *req) {
if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "WS Handshake approved.");
    return ESP_OK;
}

httpd_ws_frame_t ws_pkt;
memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
ws_pkt.type = HTTPD_WS_TYPE_TEXT;

// Krok 1: Pobierz długość przychodzącej ramki
esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
if (ret != ESP_OK) {
    return ret;
}

// Krok 2: Jeśli są dane, odczytaj je do dynamicznego bufora
if (ws_pkt.len > 0) {
    uint8_t *buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
    if (buf == NULL) return ESP_ERR_NO_MEM;
    
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Otrzymano WS z telefonu: %s", (char*)buf);
        
        if (strstr((char*)buf, "\"cmd\":\"read_dtc\"")) {
            g_req_read_dtc = true;
            ESP_LOGI(TAG, "=> Zlecono odczyt DTC (Mode 03)!");
        }
        else if (strstr((char*)buf, "\"cmd\":\"clear_dtc\"")) {
            g_req_clear_dtc = true;
            ESP_LOGI(TAG, "=> Zlecono kasowanie DTC (Mode 04)!");
        }
    }
    free(buf);
}
return ret;
}

static httpd_uri_t index_uri;
static httpd_uri_t style_uri;
static httpd_uri_t script_uri;
static httpd_uri_t ws_uri;
static httpd_uri_t log_uri;

static void init_uris(void) {
    index_uri.uri = "/"; index_uri.method = HTTP_GET; index_uri.handler = index_handler;
    style_uri.uri = "/style.css"; style_uri.method = HTTP_GET; style_uri.handler = style_handler;
    script_uri.uri = "/script.js"; script_uri.method = HTTP_GET; script_uri.handler = script_handler;
    ws_uri.uri = "/ws"; ws_uri.method = HTTP_GET; ws_uri.handler = websocket_handler; ws_uri.is_websocket = true;
    log_uri.uri = "/log"; log_uri.method = HTTP_GET; log_uri.handler = log_handler;
}

typedef struct {
    httpd_handle_t server;
    int fd;
    char payload[512];
    size_t len;
} ws_send_ctx_t;

static void ws_send_work(void *arg) {
    ws_send_ctx_t *ctx = (ws_send_ctx_t *)arg;
    if (httpd_ws_get_fd_info(ctx->server, ctx->fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
        httpd_ws_frame_t pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.payload = (uint8_t *)ctx->payload;
        pkt.len = ctx->len;
        pkt.type = HTTPD_WS_TYPE_TEXT;
        esp_err_t ret = httpd_ws_send_frame_async(ctx->server, ctx->fd, &pkt);
        if (ret != ESP_OK) ESP_LOGW(TAG, "WS send failed fd=%d: %s", ctx->fd, esp_err_to_name(ret));
    }
    free(ctx);
}

// ==========================================
// Telemetria WebSocket - JSON teraz zawiera DTC + Freeze Frame.
// Bufor zwiekszony do 512B i budowanie jest "truncation-safe": jesli
// cos by nie zmiescilo sie w buforze, ta jedna ramka jest pomijana
// (z logiem), zamiast wysylac ucietego/zepsutego JSON-a do przegladarki.
// ==========================================
static void telemetry_task(void *pvParameters) {
    while (1) {
        if (server_instance != NULL) {
            vehicle_data_t data = vehicle_data_get();
            char json_string[512];

            int len = snprintf(json_string, sizeof(json_string),
                "{\"has_data\":%s,\"rpm\":%d,\"speed\":%d,"
                "\"coolant\":%d,\"fuel\":%d,\"throttle\":%d,"
                "\"load\":%d,\"runtime\":%d,\"proto\":%d,\"frames\":%lu,"
                "\"dtc_error\":%s,\"dtcs\":[",
                data.has_data ? "true" : "false",
                data.rpm, data.speed_kmh,
                data.coolant_c, data.fuel_pct, data.throttle_pct,
                data.engine_load_pct, data.runtime_s,
                data.protocol,
                (unsigned long)data.frame_count,
                data.dtc_read_error ? "true" : "false");

            for (uint8_t i = 0; i < data.dtc_count && len > 0 && len < (int)sizeof(json_string) - 32; i++) {
                len += snprintf(json_string + len, sizeof(json_string) - len,
                                 "%s\"%s\"", (i > 0 ? "," : ""), data.dtcs[i]);
            }
            if (len > 0 && len < (int)sizeof(json_string) - 1) {
                len += snprintf(json_string + len, sizeof(json_string) - len, "]");
            }

            if (data.freeze_frame_valid && len > 0 && len < (int)sizeof(json_string) - 64) {
                len += snprintf(json_string + len, sizeof(json_string) - len,
                                 ",\"ff_rpm\":%d,\"ff_load\":%d,\"ff_coolant\":%d",
                                 data.ff_rpm, data.ff_load_pct, data.ff_coolant_c);
            }
            if (len > 0 && len < (int)sizeof(json_string) - 1) {
                len += snprintf(json_string + len, sizeof(json_string) - len, "}");
            }

            if (len <= 0 || len >= (int)sizeof(json_string)) {
                ESP_LOGE(TAG, "Telemetry JSON build failed/truncated (len=%d) - pomijam ta ramke", len);
            } else {
                size_t clients = MAX_CLIENTS;
                int client_fds[MAX_CLIENTS];

                if (httpd_get_client_list(server_instance, &clients, client_fds) == ESP_OK) {
                    for (size_t i = 0; i < clients; i++) {
                        if (httpd_ws_get_fd_info(server_instance, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                            ws_send_ctx_t *ctx = (ws_send_ctx_t *)malloc(sizeof(ws_send_ctx_t));
                            if (ctx == NULL) continue;
                            ctx->server = server_instance;
                            ctx->fd = client_fds[i];
                            ctx->len = (size_t)len;
                            memcpy(ctx->payload, json_string, ctx->len + 1);
                            if (httpd_queue_work(server_instance, ws_send_work, ctx) != ESP_OK) free(ctx);
                        }
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static httpd_handle_t start_webserver(void) {
    init_uris();
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.core_id = NET_TASK_CORE;

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
    memcpy(wifi_config.ap.ssid, "Car_Dashboard_IDF", strlen("Car_Dashboard_IDF"));
    memcpy(wifi_config.ap.password, "datalogger123", strlen("datalogger123"));
    wifi_config.ap.ssid_len = strlen("Car_Dashboard_IDF");
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// Konwertuje 2 bajty z ECU na standardowy string OBD-II (np. P0113, U1000)
// UWAGA: musi byc zdefiniowana PRZED service_dtc_requests_can(), ktora jej uzywa.
void decode_dtc(uint8_t high_byte, uint8_t low_byte, char* out_str) {
    const char letters[] = {'P', 'C', 'B', 'U'};

    // Pierwsza litera zależy od 2 najstarszych bitów
    char letter = letters[(high_byte >> 6) & 0x03];

    // Pierwsza cyfra (zwykle 0 lub 1) z 2 kolejnych bitów
    uint8_t digit1 = (high_byte >> 4) & 0x03;

    // Reszta to czysty HEX
    uint8_t digit2 = high_byte & 0x0F;
    uint8_t digit3 = (low_byte >> 4) & 0x0F;
    uint8_t digit4 = low_byte & 0x0F;

    snprintf(out_str, 6, "%c%X%X%X%X", letter, digit1, digit2, digit3, digit4);
}

// ==========================================
// Obsluga zadan Mode 03 (odczyt DTC) / Mode 04 (kasowanie) / Mode 02
// (freeze frame) na CAN. Wywolywana raz na iteracje glownej petli CAN -
// nie robi nic dopoki g_req_read_dtc / g_req_clear_dtc nie zostana
// ustawione przez websocket_handler(), wiec nie wplywa na normalny
// polling PID-ow.
// ==========================================
static void service_dtc_requests_can() {
    if (g_req_read_dtc) {
        g_req_read_dtc = false; // konsumuj od razu - flaga nie moze "utknac" i spamowac zadan
        ESP_LOGI(GATEWAY_TAG, "Wysylam Mode 03 (odczyt DTC) po CAN...");

        twai_message_t req = {.identifier = 0x7DF, .data_length_code = 8,
                               .data = {0x01, 0x03, 0, 0, 0, 0, 0, 0}};
        if (twai_transmit(&req, pdMS_TO_TICKS(50)) != ESP_OK) {
            ESP_LOGW(GATEWAY_TAG, "Mode 03 TX failed");
            vehicle_data_set_dtc_error(true);
            return;
        }

        twai_message_t rx;
        if (twai_receive(&rx, pdMS_TO_TICKS(200)) == ESP_OK &&
            rx.identifier == 0x7E8 && rx.data[1] == 0x43) {

            // Pojedyncza ramka CAN (8 bajtow) miesci najwyzej 3 kody DTC
            // (1 bajt licznika + 3x2 bajty). Wiecej kodow wymagaloby
            // multi-frame ISO-TP, czego tu nie obslugujemy.
            uint8_t num_codes = rx.data[2];
            char code_buf[6];
            uint8_t decoded = 0;

            vehicle_data_clear_dtcs();
            for (int i = 0; i < num_codes && i < 3; i++) {
                uint8_t hi = rx.data[3 + i * 2];
                uint8_t lo = rx.data[4 + i * 2];
                if (hi == 0 && lo == 0) continue; // padding, nie prawdziwy kod
                decode_dtc(hi, lo, code_buf);
                vehicle_data_add_dtc(code_buf);
                decoded++;
            }
            vehicle_data_set_dtc_error(false);
            ESP_LOGI(GATEWAY_TAG, "Odczytano %d kod(ow) DTC", decoded);

            if (decoded > 0) {
                int ff_rpm = 0, ff_load = 0, ff_coolant = 0;

                twai_message_t ff = {.identifier = 0x7DF, .data_length_code = 8,
                                      .data = {0x02, 0x02, 0x0C, 0x00, 0, 0, 0, 0}}; // RPM
                twai_transmit(&ff, pdMS_TO_TICKS(50));
                twai_message_t ff_rx;
                if (twai_receive(&ff_rx, pdMS_TO_TICKS(200)) == ESP_OK && ff_rx.identifier == 0x7E8) {
                    ff_rpm = ((ff_rx.data[3] * 256) + ff_rx.data[4]) / 4;
                }

                twai_message_t ff2 = {.identifier = 0x7DF, .data_length_code = 8,
                                       .data = {0x02, 0x02, 0x04, 0x00, 0, 0, 0, 0}}; // Load
                twai_transmit(&ff2, pdMS_TO_TICKS(50));
                twai_message_t ff2_rx;
                if (twai_receive(&ff2_rx, pdMS_TO_TICKS(200)) == ESP_OK && ff2_rx.identifier == 0x7E8) {
                    ff_load = ff2_rx.data[3] * 100 / 255;
                }

                twai_message_t ff3 = {.identifier = 0x7DF, .data_length_code = 8,
                                       .data = {0x02, 0x02, 0x05, 0x00, 0, 0, 0, 0}}; // Coolant
                twai_transmit(&ff3, pdMS_TO_TICKS(50));
                twai_message_t ff3_rx;
                if (twai_receive(&ff3_rx, pdMS_TO_TICKS(200)) == ESP_OK && ff3_rx.identifier == 0x7E8) {
                    ff_coolant = ff3_rx.data[3] - 40;
                }

                vehicle_data_set_freeze_frame(ff_rpm, ff_load, ff_coolant);
            }
        } else {
            ESP_LOGW(GATEWAY_TAG, "Brak/zla odpowiedz na Mode 03");
            vehicle_data_set_dtc_error(true);
        }
    }

    if (g_req_clear_dtc) {
        g_req_clear_dtc = false;
        ESP_LOGW(GATEWAY_TAG, "Wysylam Mode 04 (kasowanie DTC) - NIEODWRACALNE!");

        twai_message_t req = {.identifier = 0x7DF, .data_length_code = 8,
                               .data = {0x01, 0x04, 0, 0, 0, 0, 0, 0}};
        twai_transmit(&req, pdMS_TO_TICKS(50));

        twai_message_t rx;
        if (twai_receive(&rx, pdMS_TO_TICKS(200)) == ESP_OK &&
            rx.identifier == 0x7E8 && rx.data[1] == 0x44) {
            ESP_LOGI(GATEWAY_TAG, "DTC skasowane - potwierdzone przez ECU");
            vehicle_data_clear_dtcs();
            vehicle_data_set_dtc_error(false);
        } else {
            ESP_LOGW(GATEWAY_TAG, "Brak potwierdzenia kasowania DTC (mogl sie nie udac)");
            vehicle_data_set_dtc_error(true);
        }
    }
}

// ==========================================
// Pętla odczytu CAN
// ==========================================
void can_active_loop() {
    ESP_LOGI(GATEWAY_TAG, "--- TRYB CAN AKTYWNY ---");
    uint8_t pids[] = {0x0C, 0x0D, 0x05, 0x04, 0x11, 0x1F};
    int pid_idx = 0;
    int consecutive_rx_fail = 0;
    const int MAX_RX_FAIL = 20; // ~1s ciszy (20 * 50ms opoznienia petli)

    while (1) {
        twai_message_t req = {.identifier = 0x7DF, .data_length_code = 8, .data = {0x02, 0x01, pids[pid_idx], 0, 0, 0, 0, 0}};
        if (twai_transmit(&req, pdMS_TO_TICKS(50)) != ESP_OK) {
            ESP_LOGW(GATEWAY_TAG, "Błąd nadawania CAN - wypadamy z pętli");
            break;
        }

        twai_message_t rx;
        esp_err_t rx_ret = twai_receive(&rx, pdMS_TO_TICKS(100));

        if (rx_ret == ESP_OK && rx.identifier == 0x7E8) {
            consecutive_rx_fail = 0;
            if (pids[pid_idx] == 0x0C) vehicle_data_set_rpm(((rx.data[3] * 256) + rx.data[4]) / 4);
            else if (pids[pid_idx] == 0x0D) vehicle_data_set_speed(rx.data[3]);
            else if (pids[pid_idx] == 0x05) vehicle_data_set_coolant(rx.data[3] - 40);
            else if (pids[pid_idx] == 0x04) vehicle_data_set_engine_load(rx.data[3] * 100 / 255); // Obciążenie
            else if (pids[pid_idx] == 0x11) vehicle_data_set_throttle(rx.data[3] * 100 / 255);
            else if (pids[pid_idx] == 0x1F) vehicle_data_set_runtime((rx.data[3] * 256) + rx.data[4]);
        } else {
            consecutive_rx_fail++;
                if (pids[pid_idx] != 0x1F) {
                    ESP_LOGW(GATEWAY_TAG, "Brak odpowiedzi na PID 0x%02X (err=%s, streak=%d/%d)",
                     pids[pid_idx], esp_err_to_name(rx_ret), consecutive_rx_fail, MAX_RX_FAIL);
                }
                if (consecutive_rx_fail >= MAX_RX_FAIL) {
                    ESP_LOGE(GATEWAY_TAG, "ECU przestało odpowiadać - wypadamy z pętli CAN");
                    break;
                }
            }
        pid_idx = (pid_idx + 1) % 6;
        service_dtc_requests_can(); // nic nie robi, dopoki nie ma zadania z przegladarki
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGW(GATEWAY_TAG, "Utracono połączenie CAN.");
}

// ==========================================
// Pętla odczytu K-Line
// UWAGA: Mode 03/04/02 NIE sa jeszcze zaimplementowane dla K-Line - ramki
// KWP2000/ISO9141 dla tych trybow nie zostaly zweryfikowane na sprzecie.
// Zamiast po cichu ignorowac zadanie (co zawieszaloby przycisk w UI na
// zawsze), konsumujemy flage i od razu zglaszamy blad, zeby przegladarka
// pokazala uzytkownikowi ze operacja sie nie powiodla.
// ==========================================
void kline_active_loop() {
    ESP_LOGI(GATEWAY_TAG, "--- TRYB K-LINE AKTYWNY ---");
    uint8_t pids[] = {0x0C, 0x0D, 0x05, 0x04, 0x11,0x1F};
    int pid_idx = 0;

    while (1) {
        uint8_t req[] = {0x68, 0x6A, 0xF1, 0x01, pids[pid_idx], (uint8_t)(0x68+0x6A+0xF1+0x01+pids[pid_idx])};
        uart_write_bytes(KLINE_UART_NUM, (const char*)req, 6);

        uint8_t echo[6];
        uart_read_bytes(KLINE_UART_NUM, echo, 6, pdMS_TO_TICKS(50));

        uint8_t resp[16];
        int len = uart_read_bytes(KLINE_UART_NUM, resp, sizeof(resp), pdMS_TO_TICKS(150));

        if (len < 6) {
            ESP_LOGW(GATEWAY_TAG, "Brak odpowiedzi K-Line - wypadamy z pętli");
            break;
        }

        if (resp[3] == 0x41 && resp[4] == pids[pid_idx]) {
            if (pids[pid_idx] == 0x0C) vehicle_data_set_rpm(((resp[5] * 256) + resp[6]) / 4);
            else if (pids[pid_idx] == 0x0D) vehicle_data_set_speed(resp[5]);
            else if (pids[pid_idx] == 0x05) vehicle_data_set_coolant(resp[5] - 40);
            else if (pids[pid_idx] == 0x04) vehicle_data_set_engine_load(resp[5] * 100 / 255);
            else if (pids[pid_idx] == 0x11) vehicle_data_set_throttle(resp[5] * 100 / 255);
            else if (pids[pid_idx] == 0x1F) vehicle_data_set_runtime((resp[5] * 256) + resp[6]);
        }

        if (g_req_read_dtc || g_req_clear_dtc) {
            ESP_LOGW(GATEWAY_TAG, "DTC read/clear zadane, ale nie jest jeszcze zaimplementowane dla K-Line");
            g_req_read_dtc = false;
            g_req_clear_dtc = false;
            vehicle_data_set_dtc_error(true);
        }

        pid_idx = (pid_idx + 1) % 6;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGW(GATEWAY_TAG, "Utracono połączenie K-Line.");
}

// ==========================================
// Menedżer Auto-Discovery (CAN -> Fast Init -> 5-Baud Init)
// ==========================================
static void auto_discovery_task(void *arg) {
    while (1) {
        ESP_LOGI(GATEWAY_TAG, "Szukam pojazdu po CAN...");

        // --- KROK 1: TWAI (CAN) ---
        twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
        twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
        twai_filter_config_t f_config = {.acceptance_code = (0x7E8U << 21), .acceptance_mask = ~(0x7FFU << 21), .single_filter = true};

        if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
            if (twai_start() == ESP_OK) {
                twai_message_t ping = {.identifier = 0x7DF, .data_length_code = 8, .data = {0x02, 0x01, 0x0C, 0, 0, 0, 0, 0}};
                twai_transmit(&ping, pdMS_TO_TICKS(50));

                twai_message_t rx;
                if (twai_receive(&rx, pdMS_TO_TICKS(500)) == ESP_OK && rx.identifier == 0x7E8) {
                    can_active_loop(); // Sukces CAN!
                }
                twai_stop();
            }
            twai_driver_uninstall();
        }

        ESP_LOGW(GATEWAY_TAG, "Brak silnika na CAN. Szukam po K-Line (Fast Init)...");
        vTaskDelay(pdMS_TO_TICKS(500));

        // --- KROK 2: K-Line (Fast Init - KWP2000) ---
        uart_config_t uart_config = {.baud_rate = 10400, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_APB};
        uart_driver_install(KLINE_UART_NUM, 256, 256, 0, NULL, 0);
        uart_param_config(KLINE_UART_NUM, &uart_config);

        gpio_reset_pin(KLINE_TX_PIN);
        gpio_set_direction(KLINE_TX_PIN, GPIO_MODE_OUTPUT);
        gpio_set_level(KLINE_TX_PIN, 1); vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(KLINE_TX_PIN, 0); vTaskDelay(pdMS_TO_TICKS(25));
        gpio_set_level(KLINE_TX_PIN, 1); vTaskDelay(pdMS_TO_TICKS(25));

        uart_set_pin(KLINE_UART_NUM, KLINE_TX_PIN, KLINE_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        uart_flush_input(KLINE_UART_NUM);

        uint8_t start_req[] = {0xC1, 0x33, 0xF1, 0x81, 0x66};
        uart_write_bytes(KLINE_UART_NUM, (const char*)start_req, 5);

        uint8_t echo[5];
        uart_read_bytes(KLINE_UART_NUM, echo, 5, pdMS_TO_TICKS(50));

        uint8_t resp[16];
        if (uart_read_bytes(KLINE_UART_NUM, resp, sizeof(resp), pdMS_TO_TICKS(300)) > 0) {
            kline_active_loop(); // Sukces K-Line Fast Init!
        }

        uart_driver_delete(KLINE_UART_NUM);

        ESP_LOGW(GATEWAY_TAG, "Brak odpowiedzi Fast Init. Szukam po K-Line (5-Baud Init)...");
        vTaskDelay(pdMS_TO_TICKS(500));

        // --- KROK 3: K-Line (5-Baud Init - starsze ISO 9141-2) ---
        gpio_reset_pin(KLINE_TX_PIN);
        gpio_set_direction(KLINE_TX_PIN, GPIO_MODE_OUTPUT);

        // Bitbang 0x33 (200ms na bit)
        gpio_set_level(KLINE_TX_PIN, 1); vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(KLINE_TX_PIN, 0); vTaskDelay(pdMS_TO_TICKS(200)); // Start
        gpio_set_level(KLINE_TX_PIN, 1); vTaskDelay(pdMS_TO_TICKS(400)); // 1, 1
        gpio_set_level(KLINE_TX_PIN, 0); vTaskDelay(pdMS_TO_TICKS(400)); // 0, 0
        gpio_set_level(KLINE_TX_PIN, 1); vTaskDelay(pdMS_TO_TICKS(400)); // 1, 1
        gpio_set_level(KLINE_TX_PIN, 0); vTaskDelay(pdMS_TO_TICKS(400)); // 0, 0
        gpio_set_level(KLINE_TX_PIN, 1); vTaskDelay(pdMS_TO_TICKS(200)); // Stop

        uart_driver_install(KLINE_UART_NUM, 256, 256, 0, NULL, 0);
        uart_param_config(KLINE_UART_NUM, &uart_config);
        uart_set_pin(KLINE_UART_NUM, KLINE_TX_PIN, KLINE_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        uart_flush_input(KLINE_UART_NUM);

        uint8_t kwp_resp[3];
        int kwp_len = uart_read_bytes(KLINE_UART_NUM, kwp_resp, sizeof(kwp_resp), pdMS_TO_TICKS(600));

        if (kwp_len == 3 && kwp_resp[0] == 0x55) {
            uint8_t kw2_inv = ~kwp_resp[2];
            uart_write_bytes(KLINE_UART_NUM, (const char*)&kw2_inv, 1);

            uint8_t echo_kw2[1]; uart_read_bytes(KLINE_UART_NUM, echo_kw2, 1, pdMS_TO_TICKS(50));

            uint8_t ack[1];
            if (uart_read_bytes(KLINE_UART_NUM, ack, 1, pdMS_TO_TICKS(200)) > 0) {
                kline_active_loop(); // Sukces K-Line 5-Baud!
            }
        }

        uart_driver_delete(KLINE_UART_NUM);

        ESP_LOGE(GATEWAY_TAG, "Brak kompatybilnego ECU. Odpoczywam przed ponownym skanowaniem...");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_log_set_vprintf(web_log_vprintf);
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

    // Uruchomienie inteligentnej bramki Auto-Discovery na rdzeniu 1
    xTaskCreatePinnedToCore(auto_discovery_task, "gateway", 4096, NULL, 5, NULL, 1);

    // Uruchomienie serwera telemetrycznego na rdzeniu 0
    xTaskCreatePinnedToCore(telemetry_task, "telemetry", 4096, NULL, 5, NULL, NET_TASK_CORE);
}

} // extern "C"