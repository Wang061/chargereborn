#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void net_softap_start(void);   // 启动 WiFi softAP
void net_http_start(void);     // 启动 HTTP server
void net_stream_start(void);   // 启动 81 口 MJPEG 取景流

#ifdef __cplusplus
}
#endif
