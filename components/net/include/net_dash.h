#pragma once
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// 注册 dashboard 相关 URI handler(/dash/*、/battery_log)到已有的 httpd 实例。
// 调用前 server 必须已 httpd_start() 成功，且 server 的 config.uri_match_fn
// 必须已设为 httpd_uri_match_wildcard(由调用方在 httpd_config_t 里配置，
// 见 http_srv.c 的 net_http_start)。只应在系统启动时调用一次。
void net_dash_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
