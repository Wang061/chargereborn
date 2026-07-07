idf_component_register(
    SRCS "armcal.c" "homography.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash
)
