idf_component_register(
    SRCS "ai.cpp" "battery_angle.cpp"
    INCLUDE_DIRS "include"
    REQUIRES camera esp-dl battery_detect4
)
