idf_component_register(SRCS "wifi_ap.c" "http_srv.c" "stream_srv.c" "net_dash_core.c" "net_dash.c"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "../../chargereborn-dashboard/dashboard/index.html"
                                      "../../chargereborn-dashboard/dashboard/app.js"
                                      "../../chargereborn-dashboard/dashboard/styles.css"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server camera ai armlink armcal armctrl)
