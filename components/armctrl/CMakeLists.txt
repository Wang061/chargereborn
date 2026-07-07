idf_component_register(
    SRCS "armctrl.c"
    INCLUDE_DIRS "include"
    REQUIRES armcal kinematics armlink
    PRIV_REQUIRES driver nvs_flash
)
