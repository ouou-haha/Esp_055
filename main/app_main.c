#include "app_ctx.h"
#include "mqtt_app.h"
#include "wifi_connect.h"
const char *TAG = "app_main";

void app_main(void)
{

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    /* wifi debug */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);  
    /* cpu info */
    esp_log_level_set("cpu_start", ESP_LOG_WARN);

    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if 1
    ESP_ERROR_CHECK(wifi_connect("huawei", "@ouhaha6666"));
#else
    ESP_ERROR_CHECK(example_connect());
#endif
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    init_topics();

    sample_q = xQueueCreate(256, sizeof(sample_t)); //TODO: update the size of queue?
    configASSERT(sample_q);
    
    xTaskCreate(stat_task,   "stat_task",   4096, NULL, 4, NULL); // show performance
    xTaskCreate(sampler_task,"sampler_task",4096, NULL, 6, NULL);
    xTaskCreate(sender_task, "sender_task", 4096, NULL, 5, NULL);

    mqtt_app_start();
}
