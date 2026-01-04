#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"

#include "app_ctx.h"
#include "sampler.h"

QueueHandle_t sample_q = NULL;

/* performance */
uint32_t g_sample_cnt   = 0;
uint32_t g_drop_cnt     = 0;
uint32_t g_pub_ok       = 0;
uint32_t g_pub_fail     = 0;
uint32_t g_q_highwater  = 0;

void sampler_task(void *arg)
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

void sender_task(void *arg)
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
            int n = snprintf(payload, sizeof(payload), //TODO: use lighter format like csv? bin?
                             "%" PRIu32 ",%" PRIi64,
                             s.seq, s.ts_us);
#endif

            int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, payload, n, 0, 0); //TODO: publish 3 pactets each time and add a random delay(30ms~ 33.3ms)
            if (msg_id < 0) {
                g_pub_fail++;
            } else {
                g_pub_ok++;
            }
        }
    }
}

/* print performance */
void stat_task(void *arg)
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