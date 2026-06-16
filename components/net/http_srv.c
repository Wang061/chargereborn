#include "net.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include <string.h>
#include "camera.h"

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

static esp_err_t capture_get(httpd_req_t *req)
{
    // 解析 ?name=<类名>，只保留 [0-9A-Za-z_-]，缺省 "cap"
    char name[32] = "cap";
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 128) {
        char q[128];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
            char val[32];
            if (httpd_query_key_value(q, "name", val, sizeof(val)) == ESP_OK && val[0]) {
                size_t j = 0;
                for (size_t i = 0; val[i] && j < sizeof(name) - 1; i++) {
                    char c = val[i];
                    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') || c == '_' || c == '-') {
                        name[j++] = c;
                    }
                }
                name[j] = '\0';
                if (j == 0) strcpy(name, "cap");
            }
        }
    }

    camera_fb_t *fb = camera_capture();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture failed");
        return ESP_FAIL;
    }

    static uint32_t seq = 0;
    char disp[96];
    snprintf(disp, sizeof(disp),
             "attachment; filename=\"%s_%" PRIu32 "_%" PRIu32 ".jpg\"",
             name, esp_log_timestamp(), seq++);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    esp_err_t r = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    camera_return(fb);   // 与 camera_capture 配对
    return r;
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
    httpd_uri_t capture = { .uri = "/capture", .method = HTTP_GET, .handler = capture_get };
    httpd_register_uri_handler(server, &capture);
    ESP_LOGI(TAG, "http server up -> http://192.168.4.1/");
}
