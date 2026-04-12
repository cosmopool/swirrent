#pragma once

#include <stdbool.h>

#include "core.h"

#define MAX_THREADS 2
#define MAX_JOBS 1000

typedef struct {
  u32 id;
  bool processing;
  // bool initialized;
  i32 (*callback)(void *args, void *result);
  void *args;
  void *results;
} ThreadJob;

bool threadJobIsEmpty(ThreadJob);
ThreadJob threadGetCompletedJob();
void threadJobCreate(ThreadJob job);
void threadJobDestroy(u32 idx);
void threadProcessJob(void *args);
u32 threadInit();
u32 threadDeinit();
