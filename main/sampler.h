#pragma once
#define SAMPLE_PERIOD_TICKS pdMS_TO_TICKS(SAMPLE_PERIOD_MS)


void stats_start(void);
void sampler_start(void);
void sender_start(void);