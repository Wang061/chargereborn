#include "net.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"

static const char *TAG = "http_srv";

static esp_err_t root_get(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>ChargeReborn Brain</title></head><body>"
        "<h1>ChargeReborn Brain alive</h1>"
        "<p>edge-AI vision spine - phase 1</p>"
        "<p><a href=\"/status\">/status</a></p></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, html);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char buf[160];
    int n = snprintf(buf, sizeof(buf),
        "{\"uptime_s\":%" PRIu32 ",\"heap_free\":%" PRIu32 ",\"psram_free\":%u}",
        (uint32_t)(esp_log_timestamp() / 1000U),
        (uint32_t)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

void net_http_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    httpd_uri_t root   = { .uri = "/",       .method = HTTP_GET, .handler = root_get };
    httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = status_get };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    ESP_LOGI(TAG, "http server up -> http://192.168.4.1/");
}
