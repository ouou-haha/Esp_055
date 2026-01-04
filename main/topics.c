#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "app_ctx.h"
#include "topics.h"

char g_dev_id[16] = {0};
char g_topic_data[64];
char g_topic_status[64];

void init_topics(void)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(g_dev_id, sizeof(g_dev_id), "%02x%02x%02x", mac[3], mac[4], mac[5]);

    snprintf(g_topic_data, sizeof(g_topic_data), "sensors/%s/data", g_dev_id);
    snprintf(g_topic_status, sizeof(g_topic_status), "sensors/%s/status", g_dev_id);

    ESP_LOGI(TAG, "dev_id=%s", g_dev_id);
    ESP_LOGI(TAG, "topic_data=%s", g_topic_data);
    ESP_LOGI(TAG, "topic_status=%s", g_topic_status);
}

