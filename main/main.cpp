#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

#define MAX_CLIENTS 4

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
    httpd_resp_send(req, (const char *)script_js_start, script_js_end - script_js_start);
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

static void telemetry_mock_task(void *pvParameters) {
    int current_rpm = 800;
    int direction   = 150;

    while (1) {
        if (server_instance != NULL) {
            current_rpm += direction;
            if (current_rpm >= 7200) { current_rpm = 7200; direction = -200; }
            if (current_rpm <= 900)  { current_rpm = 900;  direction =  150; }

            int    current_speed = current_rpm / 30;
            double boost = (current_rpm > 3000) ? ((current_rpm - 3000) * 0.00035) : 0.0;

            char json_string[256];
            snprintf(json_string, sizeof(json_string),
                "{\"speed\":%d,\"rpm\":%d,\"boost\":%.2f,"
                "\"coolant\":92,\"oil\":104,\"voltage\":14.1,"
                "\"fuel\":68,\"temp\":19}",
                current_speed, current_rpm, boost);

            size_t clients = MAX_CLIENTS;
            int    client_fds[MAX_CLIENTS];

            if (httpd_get_client_list(server_instance, &clients, client_fds) == ESP_OK) {
                for (size_t i = 0; i < clients; i++) {
                    if (httpd_ws_get_fd_info(server_instance, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                        httpd_ws_frame_t ws_pkt;
                        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                        ws_pkt.payload = (uint8_t *)json_string;
                        ws_pkt.len     = strlen(json_string);
                        ws_pkt.type    = HTTPD_WS_TYPE_TEXT;
                        httpd_ws_send_frame_async(server_instance, client_fds[i], &ws_pkt);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static httpd_handle_t start_webserver(void) {
    init_uris();
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;

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

    xTaskCreatePinnedToCore(telemetry_mock_task, "telemetry", 4096, NULL, 5, NULL, 0);
}

} // extern "C"