#include <switch.h>

#include "core.h"
#include "log.h"
#include "threads.h"

static Thread threads[MAX_THREADS] = {0};
static i32 pending_count;
static i32 finished_count;
static u32 processing_count;
static ThreadJob pending[MAX_JOBS] = {0};
static ThreadJob finished[MAX_JOBS] = {0};
static Mutex m_pending = {0};
static Mutex m_finished = {0};
static Mutex m_processing = {0};

bool threadHasPendingJobs() {
  return pending_count > 0 || processing_count > 0 || finished_count > 0;
}

bool threadJobIsEmpty(ThreadJob job) {
  mutexLock(&m_finished);
  bool is_empty = job.idx == 0 &&
                  job.tracker_id == 0 &&
                  !job.callback &&
                  !job.args &&
                  !job.results &&
                  job.result_code == 0;
  mutexUnlock(&m_finished);
  return is_empty;
}

ThreadJob threadJobStartProcessing(u32 idx) {
  mutexLock(&m_pending);
  ThreadJob job = pending[idx];
  pending[idx] = (ThreadJob){0};
  pending_count--;
  processing_count++;
  mutexUnlock(&m_pending);
  return job;
}

ThreadJob threadJobGetCompleted() {
  mutexLock(&m_finished);
  logInfo("[THREADS] [getcomplete] fetching completed job");
  if (finished_count <= 0) {
    mutexUnlock(&m_finished);
    logInfo("[THREADS] [getcomplete] no completed job available. empty queue");
    return (ThreadJob){0};
  }
  finished_count--;
  ThreadJob job = finished[finished_count];
  ASSERT(job.callback, "a job must have a callback");
  ASSERT(job.results, " completed job should have results");
  finished[finished_count] = (ThreadJob){0};
  logInfo("[THREADS] [getcomplete] fetched job (%d)", job.idx);
  logInfo("[THREADS] [getcomplete] %d finished jobs waiting processing and %d pending", finished_count, pending_count);
  mutexUnlock(&m_finished);
  return job;
}

void threadJobComplete(ThreadJob job) {
  mutexLock(&m_processing);
  ASSERT(processing_count > 0, "must exist jobs being processed");
  processing_count--;
  mutexLock(&m_processing);

  mutexLock(&m_finished);
  logInfo("[THREADS] [complete] completing job (%d)", job.tracker_id);
  ASSERT(job.callback, "a job must have a callback");
  ASSERT(job.results || job.result_code != 0, "to complete a job must have 'results' or an error 'result_code'");
  finished[finished_count] = job;
  threadJobDestroy(job.idx);
  job.idx = finished_count;
  finished_count++;
  ASSERT(finished_count < MAX_JOBS, "can't complete more jobs then MAX_JOBS");
  logInfo("[THREADS] [complete] %d finished jobs waiting processing and %d pending", finished_count, pending_count);
  mutexUnlock(&m_finished);
}

void threadJobCreate(ThreadJob job) {
  mutexLock(&m_pending);
  logInfo("[THREADS] [create] creating job (%d)", job.tracker_id);
  ASSERT(job.callback, "a job must have a callback");
  job.idx = pending_count;
  pending[pending_count] = job;
  pending_count++;
  ASSERT(pending_count < MAX_JOBS, "can't schedule more jobs then MAX_JOBS");
  logInfo("[THREADS] [create] %d pending jobs waiting processing and %d finished", pending_count, finished_count);
  mutexUnlock(&m_pending);
}

void threadJobDestroy(u32 idx) {
  mutexLock(&m_pending);
  logInfo("[THREADS] [destroy] destroying job (%d)", idx);
  pending_count--;
  pending[idx] = pending[pending_count];
  pending[idx].idx = idx;
  pending[pending_count] = (ThreadJob){0};
  ASSERT(pending_count >= 0, "job count cannot be negative");
  logInfo("[THREADS] [destroy] %d pending jobs waiting processing and %d finished", pending_count, finished_count);
  mutexUnlock(&m_pending);
}

void threadProcessJob(void *args) {
  logInfo("[THREADS] [process] start processing jobs");
  ASSERT(args == NULL, "this function should not receive any args right now");
  while (true) {
    logInfo("[THREADS] [process] wating for jobs");
    while (pending_count == 0) {
      svcSleepThread(30 * NANOSECONDS_IN_MILLI);
    }

    i32 i = 0;
    if (mutexTryLock(&m_pending) == 0) continue;
    logDebug("[THREADS] [process] %d jobs available for processing", pending_count);
    ThreadJob job = {0};
    for (i = 0; i < pending_count; i++) {
      if (threadJobIsEmpty(pending[i])) continue;
      job = threadJobStartProcessing(i);
      ASSERT(job.callback != NULL, "all jobs should be initialized and callbacks assigned");
      break;
    }
    if (threadJobIsEmpty(job)) continue;
    logInfo("[THREADS] [process] processing job (%d)", i);
    mutexUnlock(&m_pending);
    job.result_code = job.callback(job.args, &job.results);
    threadJobComplete(job);
  }
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
