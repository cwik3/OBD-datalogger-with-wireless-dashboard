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

#define MAX_CLIENTS 4

// Pin WiFi/HTTP/WebSocket work to core 0; CAN tasks are pinned to core 1
// in Mock_CAN.h's can_mock_init(). Keeping these on separate cores avoids
// WiFi stack activity from jittering CAN bus timing and vice versa.
#define NET_TASK_CORE 0

extern "C" {

static const char *TAG = "DataLogger";
static httpd_handle_t server_instance = NULL;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t script_js_start[]  asm("_binary_script_js_start");
extern const uint8_t script_js_end[]    asm("_binary_script_js_end");

esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

esp_err_t style_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)style_css_start, style_css_end - style_css_start);
    return ESP_OK;
}

esp_err_t script_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript");
    size_t len = script_js_end - script_js_start;
    if (len > 0 && script_js_start[len - 1] == '\0') len--;
    httpd_resp_send(req, (const char *)script_js_start, len);
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

static void init_uris(void) {
    index_uri.uri     = "/";          index_uri.method  = HTTP_GET; index_uri.handler  = index_handler;
    style_uri.uri     = "/style.css"; style_uri.method  = HTTP_GET; style_uri.handler  = style_handler;
    script_uri.uri    = "/script.js"; script_uri.method = HTTP_GET; script_uri.handler = script_handler;
    ws_uri.uri        = "/ws";        ws_uri.method     = HTTP_GET; ws_uri.handler     = websocket_handler;
    ws_uri.is_websocket = true;
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

void app_main(void) {
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
    can_mock_init();

    xTaskCreatePinnedToCore(telemetry_task, "telemetry", 4096, NULL, 5, NULL, NET_TASK_CORE);
}

} // extern "C"