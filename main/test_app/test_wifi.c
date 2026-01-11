// main/test_app/test_wifi.c
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "wifi_connect.h"
#include "esp_wifi.h"

static const char *TAG = "test_wifi";

void app_main(void)
{
    ESP_LOGI(TAG, "=== WiFi TEST APP START ===");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if 1
    ESP_ERROR_CHECK(wifi_connect("huawei", "@ouhaha6666"));
#else
    ESP_ERROR_CHECK(example_connect());
#endif

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "=== WiFi TEST DONE ===");

}
