#include "net.h"
#include "camera.h"
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_http_server.h"

static const char *TAG = "stream_srv";

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY     = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_get(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char part[64];
    while (true) {
        camera_fb_t *fb = camera_capture();
        if (!fb) { res = ESP_FAIL; break; }

        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) {
            int hl = snprintf(part, sizeof(part), STREAM_PART, (unsigned)fb->len);
            res = (hl > 0 && hl < (int)sizeof(part))   // 防 snprintf 截断
                ? httpd_resp_send_chunk(req, part, hl) : ESP_FAIL;
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        camera_return(fb);     // 与 camera_capture 配对
        if (res != ESP_OK) break;   // 客户端断开 → 退出循环
    }
    ESP_LOGI(TAG, "stream client closed");
    return res;
}

void net_stream_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port   = 32769;     // 与 80 服务默认 ctrl_port(32768) 错开
    config.stack_size  = 8192;      // 流 handler + 相机,留余量防栈溢出
    config.lru_purge_enable = true; // 单观看者采集够用;多观看者需调大 max_open_sockets(LRU 可能踢掉活跃流)
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "stream httpd_start failed");
        return;
    }
    httpd_uri_t stream = { .uri = "/stream", .method = HTTP_GET, .handler = stream_get };
    httpd_register_uri_handler(server, &stream);
    ESP_LOGI(TAG, "stream server up -> http://192.168.4.1:81/stream");
}
