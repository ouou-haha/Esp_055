#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "app_ctx.h"
#include "sampler.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"



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
        if ((uint32_t)qlen > g_q_highwater) g_q_highwater = (uint32_t)qlen;
#else
        if (xQueueSend(sample_q, &s, 0) != pdTRUE) { 
            g_drop_cnt++; 
        } else { 
            UBaseType_t qlen = uxQueueMessagesWaiting(sample_q); 
            if (qlen > g_q_highwater) g_q_highwater = qlen; 
        }
#endif
        vTaskDelayUntil(&last, SAMPLE_PERIOD_TICKS);
    }
}

static void sender_task(void *arg)
{
    char payload[128];

    while (1) {
        sample_t s;
        if (xQueueReceive(sample_q, &s, portMAX_DELAY) == pdTRUE) {

            if (!mqtt_connected) continue;
#if 0
            int n = snprintf(payload, sizeof(payload),
                             "{\"seq\":%u,\"ts_us\":%lld}",
                             (unsigned)s.seq, (long long)s.ts_us);
#else
            int n = snprintf(payload, sizeof(payload), //csv
                 "%" PRIu32 ",%" PRIi64,
                 s.seq, s.ts_us);

#endif

            int msg_id = esp_mqtt_client_publish(g_mqtt_client, g_topic_data, payload, n, 0, 0);
            if (msg_id < 0) g_pub_fail++;
            else g_pub_ok++;
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
                 g_sample_cnt, g_drop_cnt, g_pub_ok, g_pub_fail,
                 (uint32_t)uxQueueMessagesWaiting(sample_q),
                 g_q_highwater,
                 (int)mqtt_connected);

        g_sample_cnt = g_drop_cnt = g_pub_ok = g_pub_fail = 0;
        g_q_highwater = 0;
    }
}

void stats_start(void)
{
    xTaskCreate(stat_task, "stat_task", 4096, NULL, 4, NULL);
}

void sender_start(void)
{
    xTaskCreate(sender_task, "sender_task", 4096, NULL, 5, NULL);
}

void sampler_start(void)
{
    xTaskCreate(sampler_task, "sampler_task", 4096, NULL, 6, NULL);
}