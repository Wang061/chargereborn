idf_component_register(
    SRCS "voicelink.c" "voicelink_frame.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver armctrl
)
