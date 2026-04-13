#pragma once

#include <stdbool.h>

#include "core.h"

#define MAX_THREADS 32
#define MAX_JOBS 1000

typedef struct {
  u32 tracker_id;
  u32 idx;
  i32 (*callback)(void *args, void **result);
  void *args;
  void *results;
  u32 result_code;
} ThreadJob;

bool threadPoolHasWork();
bool threadJobIsZero(ThreadJob);
ThreadJob threadJobDequeue();
void threadJobEnqueue(ThreadJob job);
u32 threadPoolInit();
u32 threadPoolDeinit();
