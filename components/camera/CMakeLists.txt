# REQUIRES（非 PRIV）：camera.h 公开 #include "esp_camera.h"，需让 net/main 也拿到该头
idf_component_register(SRCS "camera.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp32-camera)
