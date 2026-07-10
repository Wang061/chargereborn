#include "net_dash.h"
#include "net_dash_core.h"
#include "esp_log.h"

static const char *TAG = "net_dash";

// EMBED_TXTFILES 生成的符号(见 CMakeLists.txt)：文件名中的 '.' 变成 '_'。
// EMBED_TXTFILES 会自动追加 NUL 终止符，故用 HTTPD_RESP_USE_STRLEN 发送即可，
// 不需要手动算 (_end - _start)。
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t styles_css_start[] asm("_binary_styles_css_start");

static esp_err_t dash_get(httpd_req_t *req)
{
    switch (dash_classify_uri(req->uri)) {
    case DASH_ROUTE_INDEX:
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, (const char *)index_html_start, HTTPD_RESP_USE_STRLEN);
    case DASH_ROUTE_APP_JS:
        httpd_resp_set_type(req, "application/javascript; charset=utf-8");
        return httpd_resp_send(req, (const char *)app_js_start, HTTPD_RESP_USE_STRLEN);
    case DASH_ROUTE_STYLES_CSS:
        httpd_resp_set_type(req, "text/css; charset=utf-8");
        return httpd_resp_send(req, (const char *)styles_css_start, HTTPD_RESP_USE_STRLEN);
    default:
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "unknown /dash asset");
        return ESP_FAIL;
    }
}

void net_dash_register(httpd_handle_t server)
{
    httpd_uri_t dash = { .uri = "/dash/?*", .method = HTTP_GET, .handler = dash_get };
    httpd_register_uri_handler(server, &dash);
    ESP_LOGI(TAG, "dash registered: /dash/");
}
