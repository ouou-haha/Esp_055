#pragma once
#define SAMPLE_PERIOD_TICKS pdMS_TO_TICKS(SAMPLE_PERIOD_MS)

void stat_task(void *arg);
void sampler_task(void *arg);
void sender_task(void *arg);