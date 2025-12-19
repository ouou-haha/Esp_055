/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_mac.h"

#include "protocol_examples_common.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt_example";

#define SAMPLE_PERIOD_MS     10
#define SAMPLE_PERIOD_TICKS  pdMS_TO_TICKS(SAMPLE_PERIOD_MS)

static char g_dev_id[16] = {0};
static char g_topic_data[64];
static char g_topic_status[64];

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

esp_mqtt_client_handle_t client;
static volatile bool mqtt_connected = false;

static uint32_t g_sample_cnt   = 0;
static uint32_t g_drop_cnt     = 0;
static uint32_t g_pub_ok       = 0;
static uint32_t g_pub_fail     = 0;
static uint32_t g_q_highwater  = 0;

typedef struct {
    uint32_t seq;
    int64_t  ts_us;
} sample_t;

static QueueHandle_t sample_q = NULL;

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

            int msg_id = esp_mqtt_client_publish(client, topic, payload, n, 0, 0);
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

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "",
             base, event_id);

    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_connected = true;

        esp_mqtt_client_publish(client, g_topic_status, "online", 0, 0, 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        // g_pub_ack++;
        // ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls",
                                 event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack",
                                 event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",
                                 event->error_handle->esp_transport_sock_errno);

            ESP_LOGI(TAG, "Last errno string (%s)",
                     strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,

        .session = {
            .keepalive = 15,
            .disable_clean_session = false,
        },

        .network = {
            .reconnect_timeout_ms = 1000,
        },

        .session.last_will = {
            .topic   = g_topic_status,
            .msg     = "offline",
            .msg_len = 0,     // 0 表示用 strlen(msg)
            .qos     = 0,
            .retain  = 1,
        },
    };

#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
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
