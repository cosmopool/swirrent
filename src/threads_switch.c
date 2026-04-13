#include <switch.h>

#include "core.h"
#include "log.h"
#include "threads.h"

static Thread threads[MAX_THREADS] = {0};
static u32 pending_count;
static u32 finished_count;
static u32 processing_count;
static ThreadJob pending[MAX_JOBS] = {0};
static ThreadJob finished[MAX_JOBS] = {0};
static Mutex m_pending = {0};
static Mutex m_finished = {0};
static Mutex m_processing = {0};

bool threadsPoolHasWork() {
  return pending_count > 0 || processing_count > 0 || finished_count > 0;
}

bool threadsJobIsEmpty(ThreadJob job) {
  bool is_zero = job.idx == 0 &&
                 job.tracker_id == 0 &&
                 !job.callback &&
                 !job.args &&
                 !job.results &&
                 job.error == 0;
  return is_zero;
}

ThreadJob _threadsjobClaimFromPending(u32 idx) {
  mutexLock(&m_processing);
  ThreadJob job = pending[idx];
  processing_count++;
  pending_count--;
  pending[idx] = pending[pending_count];
  pending[idx].idx = idx;
  pending[pending_count] = (ThreadJob){0};
  mutexUnlock(&m_processing);
  return job;
}

ThreadJob threadsJobTakeFinished() {
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
  ASSERT(job.results || job.error != 0, "a completed job should have results");
  finished[finished_count] = (ThreadJob){0};
  logInfo("[THREADS] [getcomplete] fetched job (%d)", job.idx);
  logInfo("[THREADS] [getcomplete] finished: %d | pending: %d | processing: %d", finished_count, pending_count, processing_count);
  mutexUnlock(&m_finished);
  return job;
}

void threadsJobMoveToFinished(ThreadJob job) {
  mutexLock(&m_processing);
  ASSERT(processing_count > 0, "must exist jobs being processed");
  processing_count--;
  mutexUnlock(&m_processing);

  mutexLock(&m_finished);
  logInfo("[THREADS] [complete] completing job (%d)", job.tracker_id);
  ASSERT(job.callback, "a job must have a callback");
  ASSERT(job.results || job.error != 0, "to complete a job must have 'results' or an error 'result_code'");
  finished[finished_count] = job;
  job.idx = finished_count;
  finished_count++;
  ASSERT(finished_count < MAX_JOBS, "can't complete more jobs then MAX_JOBS");
  logInfo("[THREADS] [complete] %d finished jobs waiting processing and %d pending", finished_count, pending_count);
  mutexUnlock(&m_finished);
}

void threadsJobEnqueue(ThreadJob job) {
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

void workerLoop(void *args) {
  logInfo("[THREADS] [process] start processing jobs");
  ASSERT(args == NULL, "this function should not receive any args right now");
  while (true) {
    while (pending_count == 0) {
      svcSleepThread(30 * NANOSECONDS_IN_MILLI);
    }

    u32 i = 0;
    mutexLock(&m_pending);
    logDebug("[THREADS] [process] %d jobs available for processing", pending_count);
    ThreadJob job = {0};
    for (i = 0; i < pending_count; i++) {
      if (threadsJobIsEmpty(pending[i])) continue;
      job = _threadsjobClaimFromPending(i);
      ASSERT(job.callback != NULL, "all initialized jobs should have callbacks assigned");
      break;
    }
    if (threadsJobIsEmpty(job)) continue;
    logInfo("[THREADS] [process] processing job (%d)", i);
    mutexUnlock(&m_pending);
    job.error = job.callback(job.args, &job.results);
    threadsJobMoveToFinished(job);
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
