#include <switch.h>

#include "core.h"
#include "log.h"
#include "threads.h"

static Thread threads[MAX_THREADS] = {0};
static i32 pending_count;
static i32 finished_count;
static ThreadJob pending[MAX_JOBS] = {0};
static ThreadJob finished[MAX_JOBS] = {0};
static Mutex m_pending = {0};
static Mutex m_finished = {0};

static bool isEmptyJob(u32 i) {
  return pending[i].id == 0 &&
         pending[i].processing == false &&
         pending[i].callback == NULL;
}

bool threadJobIsEmpty(ThreadJob j) {
  return !j.args && !j.processing && !j.callback && !j.results && j.id == 0;
};

ThreadJob threadGetCompletedJob() {
  logInfo("[THREADS] fetching completed job");
  // ASSERT(!job.processing, "a job cannot start with 'processing == true'. the thread that controls this value");
  // ASSERT(job.callback, "a job must have a callback");
  // ASSERT(job.results, "to complete a job the 'results' pointer must be not null");
  mutexLock(&m_finished);
  finished_count--;
  ThreadJob job = finished[finished_count];
  finished[finished_count] = (ThreadJob){0};
  mutexUnlock(&m_finished);
  logInfo("[THREADS] fetched %d", job.id);
  logInfo("[THREADS] %d finished jobs waiting processing", finished_count);
  return job;
}

void threadJobComplete(ThreadJob job) {
  logInfo("[THREADS] completing job (%d)", job.id);
  ASSERT(!job.processing, "a job cannot start with 'processing == true'. the thread that controls this value");
  ASSERT(job.callback, "a job must have a callback");
  ASSERT(job.results, "to complete a job the 'results' pointer must be not null");
  threadJobDestroy(job.idx);
  mutexLock(&m_finished);
  finished[finished_count] = job;
  finished_count++;
  mutexUnlock(&m_finished);
  logInfo("[THREADS] %d finished jobs waiting processing", finished_count);
}

void threadJobCreate(ThreadJob job) {
  logInfo("[THREADS] creating job (%d)", job.id);
  ASSERT(!job.processing, "a job cannot start with 'processing == true'. the thread that controls this value");
  ASSERT(job.callback, "a job must have a callback");
  job.id = pending_count;
  mutexLock(&m_pending);
  pending[pending_count] = job;
  pending_count++;
  mutexUnlock(&m_pending);
  logInfo("[THREADS] %d pending jobs waiting processing", pending_count);
}

void threadJobDestroy(u32 idx) {
  logInfo("[THREADS] destroying job (%d)", idx);
  ASSERT(!pending[idx].processing, "should not destroy a task that is being processed.");
  mutexLock(&m_pending);
  pending[idx] = pending[pending_count];
  pending[pending_count] = (ThreadJob){0};
  pending_count--;
  mutexUnlock(&m_pending);
  ASSERT(pending_count >= 0, "job count cannot be negative");
  logInfo("[THREADS] %d pending jobs waiting processing", pending_count);
}

void threadProcessJob(void *args) {
  // logInfo("[THREADS] start processing jobs");
  ASSERT(args == NULL, "this function should not receive any args right now");
  while (true) {
    // logInfo("[THREADS] wating for jobs");
    while (pending_count == 0) {
      svcSleepThread(30 * NANOSECONDS_IN_MILLI);
    }

    i32 i = 0;
    if (mutexTryLock(&m_pending) == 0) continue;
    // logDebug("[THREADS] %d jobs available for processing", pending_count);
    for (i = 0; i < pending_count; i++) {
      if (isEmptyJob(i)) continue;
      if (pending[i].processing) continue;
      ASSERT(pending[i].callback, "all jobs should be initialized and callbacks assigned");
      pending[i].processing = true;
      break;
    }
    // logInfo("[THREADS] processing job (%d)", i);
    mutexUnlock(&m_pending);
    pending[i].callback(pending[i].args, pending[i].results);
    mutexLock(&m_pending);
    pending[i].processing = false;
    mutexUnlock(&m_pending);
    threadJobComplete(pending[i]);
  }
}

u32 threadInit() {
  u32 rc = 0;
  u32 stack_size = 128 * 1024;
  for (u32 i = 0; i < MAX_THREADS; i++) {
    rc = threadCreate(threads + i, threadProcessJob, NULL, NULL, stack_size, 0x3B, 2);
    if (R_FAILED(rc)) {
      logInfo("[THREADS] threadCreate failed: 0x%x (module=%u, desc=%u)", R_VALUE(rc), R_MODULE(rc), R_DESCRIPTION(rc));
      return rc;
    }
    // logInfo("[THREADS] starting thread (%d): %d", i, threads[i].handle);
    threadStart(threads + i);
  }
  return rc;
}

u32 threadDeinit() {
  u32 rc = 0;
  for (u32 i = 0; i < MAX_THREADS; i++) {
    rc = threadWaitForExit(threads + i);
    if (R_FAILED(rc)) {
      logInfo("[THREADS] threadWaitForExit failed: 0x%x (module=%u, desc=%u)", R_VALUE(rc), R_MODULE(rc), R_DESCRIPTION(rc));
      return rc;
    }
  }
  return rc;
}
