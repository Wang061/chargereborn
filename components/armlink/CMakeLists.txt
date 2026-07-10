idf_component_register(
    SRCS "armlink.c" "armlink_uart.c" "armlink_frame.c" "target_track.c"
    INCLUDE_DIRS "include"
    REQUIRES ai
    PRIV_REQUIRES driver
)
