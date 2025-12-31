#include "app_ctx.h"
#include "mqtt_app.h"

const char *TAG = "mqtt_example";

char g_dev_id[16] = {0};
char g_topic_data[64];
char g_topic_status[64];

uint32_t g_sample_cnt   = 0;
uint32_t g_drop_cnt     = 0;
uint32_t g_pub_ok       = 0;
uint32_t g_pub_fail     = 0;
uint32_t g_q_highwater  = 0;

static void init_topics(void)
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

static void sampler_task(void *arg)
{
    uint32_t seq = 0;
    TickType_t last = xTaskGetTickCount();

    while (1) {
        sample_t s = {
            .seq   = seq++,
            .ts_us = esp_timer_get_time(),
        };

        g_sample_cnt++;

        // 队列满：这里选择“丢新样本”并统计（你也可以改成覆盖旧的）
#if 0
        if (xQueueSend(sample_q, &s, 0) != pdTRUE) {
            sample_t drop;
            if (xQueueReceive(sample_q, &drop, 0) == pdTRUE) {
                if (xQueueSend(sample_q, &s, 0) != pdTRUE) {
                    g_drop_cnt++;
                }
            } else {
                g_drop_cnt++;
            }
        }

        UBaseType_t qlen = uxQueueMessagesWaiting(sample_q);
        if ((uint32_t)qlen > g_q_highwater) {
            g_q_highwater = (uint32_t)qlen;
        }
#else
        if (xQueueSend(sample_q, &s, 0) != pdTRUE) {
            g_drop_cnt++;
        } else {
            UBaseType_t qlen = uxQueueMessagesWaiting(sample_q);
            if (qlen > g_q_highwater) {
                g_q_highwater = qlen;
            }
        }
#endif

        vTaskDelayUntil(&last, SAMPLE_PERIOD_TICKS);
    }
}

static void sender_task(void *arg)
{
    const char *topic = g_topic_data;
    char payload[128];

    while (1) {
        sample_t s;
        if (xQueueReceive(sample_q, &s, portMAX_DELAY) == pdTRUE) {

            if (!mqtt_connected) {
                continue;
            }

#if 0
            int n = snprintf(payload, sizeof(payload),
                             "{\"seq\":%u,\"ts_us\":%lld}",
                             (unsigned)s.seq, (long long)s.ts_us);
#else
            int n = snprintf(payload, sizeof(payload), // csv
                             "%" PRIu32 ",%" PRIi64,
                             s.seq, s.ts_us);
#endif

            int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, payload, n, 0, 0);
            if (msg_id < 0) {
                g_pub_fail++;
            } else {
                g_pub_ok++;
            }
        }
    }
}

static void stat_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG,
                 "1s stats: sample=%" PRIu32 " drop=%" PRIu32
                 " pub_ok=%" PRIu32 " pub_fail=%" PRIu32
                 " qlen=%" PRIu32 " highwater=%" PRIu32 " mqtt=%d",
                 (uint32_t)g_sample_cnt,
                 (uint32_t)g_drop_cnt,
                 (uint32_t)g_pub_ok,
                 (uint32_t)g_pub_fail,
                 (uint32_t)uxQueueMessagesWaiting(sample_q),
                 (uint32_t)g_q_highwater,
                 (int)mqtt_connected);

        g_sample_cnt  = 0;
        g_drop_cnt    = 0;
        g_pub_ok      = 0;
        g_pub_fail    = 0;
        g_q_highwater = 0;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    init_topics();

    sample_q = xQueueCreate(256, sizeof(sample_t)); // 256条缓存，约2.56秒
    configASSERT(sample_q);


    
    xTaskCreate(stat_task,   "stat_task",   4096, NULL, 4, NULL); // show performance
    xTaskCreate(sampler_task,"sampler_task",4096, NULL, 6, NULL);
    xTaskCreate(sender_task, "sender_task", 4096, NULL, 5, NULL);

    mqtt_app_start();
}
