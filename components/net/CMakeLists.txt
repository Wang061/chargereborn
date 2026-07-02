idf_component_register(SRCS "wifi_ap.c" "http_srv.c" "stream_srv.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server camera ai armlink armcal)
