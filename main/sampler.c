#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"

#include "app_ctx.h"
#include "sampler.h"

#define BATCH_N  3

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
        const int64_t now_us = esp_timer_get_time();
        sample_t s = {
            .seq   = seq++,
            .ts_us = now_us,
            .imu = {
                .acc      = { 0, 0, 981 },        // 固定：约 9.81 m/s^2（若你按 1m/s^2=100LSB）
                .lin_acc  = { 0, 0, 0 },          // 固定：0
                .gyro     = { 0, 0, 0 },          // 固定：0
                .quat     = { 16384, 0, 0, 0 },   // 固定：单位四元数 w=1
                .status   = {0},                  // 固定：全 0
                .tag      = 0xBAB055,             // 固定：标记
                .batt_mV  = 4100,                 // 固定：4.1V
            },
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
    char payload[256];

    while (1) {

        if (!mqtt_connected) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

#if 0
            int n = snprintf(payload, sizeof(payload),
                             "{\"seq\":%u,\"ts_us\":%lld}",
                             (unsigned)s.seq, (long long)s.ts_us);
#else
        int offset = 0;
        for (int _i = 0; _i < BATCH_N; _i++) {
            sample_t _s;
            if (xQueueReceive(sample_q, &_s, portMAX_DELAY) != pdTRUE) break;

            int left = (int)sizeof(payload) - offset;
            if (left <= 1) break;

            int n = snprintf(payload + offset, left,
                            "%" PRIu32 ",%" PRIi64 "\n", _s.seq, _s.ts_us);

            if (n < 0) break;
            if (n >= left) {
                offset = (int)sizeof(payload) - 1;
                payload[offset] = '\0';
                break;
            }

            offset += n;
        }
#endif

        if (offset > 0) {
            int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, payload, offset, 0, 0);
            if (msg_id < 0) g_pub_fail++;
            else g_pub_ok++;
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