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
  u32 error;
} ThreadJob;

bool threadsPoolHasWork();
bool threadsJobIsEmpty(ThreadJob);
ThreadJob threadsJobTakeFinished();
void threadsJobEnqueue(ThreadJob job);
u32 threadsPoolInit();
u32 threadsPoolDeinit();
