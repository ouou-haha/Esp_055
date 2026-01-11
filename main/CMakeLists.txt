# set(APP_ENTRY "app_main.c")
set(APP_ENTRY "test_app/test_wifi.c")

idf_component_register(
    SRCS
        ${APP_ENTRY}
        "topics.c"
        "mqtt_app.c"
        "sampler.c"
        "wifi_connect.c"
    INCLUDE_DIRS "."
)