#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mqtt_client.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/task.h"
#include "freertos/semphr.h"
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

#include "topics.h"
#include "mqtt_app.h"
#include "sampler.h"

#define SAMPLE_PERIOD_MS     10
#define SAMPLE_Q_LEN         256
#define SAMPLE_PERIOD_MS     10
#define SAMPLE_PERIOD_TICKS  pdMS_TO_TICKS(SAMPLE_PERIOD_MS)


extern const char *TAG;


extern char g_dev_id[16];
extern char g_topic_data[64];
extern char g_topic_status[64];

// --- MQTT ---
extern esp_mqtt_client_handle_t g_mqtt_client;
extern volatile bool mqtt_connected;

// --- perf ---
extern uint32_t g_sample_cnt;
extern uint32_t g_drop_cnt;
extern uint32_t g_pub_ok;
extern uint32_t g_pub_fail;
extern uint32_t g_q_highwater;

// --- sample & queue ---
typedef struct {
    uint32_t seq;
    int64_t  ts_us;
} sample_t;

extern QueueHandle_t sample_q;
