#include <pthread.h>
#include <stdio.h>
#include <sys/unistd.h>
#include <unistd.h>

#include "../core.h"
#include "../log.h"
#include "../threads.h"

static pthread_t threads[MAX_THREADS] = {0};
static u32 pending_count;
static u32 finished_count;
static u32 processing_count;
static ThreadJob pending[MAX_JOBS] = {0};
static ThreadJob finished[MAX_JOBS] = {0};
static pthread_mutex_t m_pending = {0};
static pthread_mutex_t m_finished = {0};
static pthread_mutex_t m_processing = {0};

bool threadPoolHasWork() {
  return pending_count > 0 || processing_count > 0 || finished_count > 0;
}

bool threadJobIsZero(ThreadJob job) {
  bool is_zero = job.idx == 0 &&
                 job.tracker_id == 0 &&
                 !job.callback &&
                 !job.args &&
                 !job.results &&
                 job.result_code == 0;
  return is_zero;
}

ThreadJob jobMoveToProcesing(u32 idx) {
  pthread_mutex_unlock(&m_processing);
  ThreadJob job = pending[idx];
  processing_count++;
  pending_count--;
  ASSERT(pending_count >= 0, "job count cannot be negative");
  pending[idx] = pending[pending_count];
  pending[idx].idx = idx;
  pending[pending_count] = (ThreadJob){0};
  pthread_mutex_unlock(&m_processing);
  return job;
}

ThreadJob threadJobDequeue() {
  pthread_mutex_lock(&m_finished);
  logInfo("[THREADS] [getcomplete] fetching completed job");
  if (finished_count <= 0) {
    pthread_mutex_unlock(&m_finished);
    logInfo("[THREADS] [getcomplete] no completed job available. empty queue");
    return (ThreadJob){0};
  }
  finished_count--;
  ThreadJob job = finished[finished_count];
  ASSERT(job.callback, "a job must have a callback");
  ASSERT(job.results || job.result_code != 0, "a completed job should have results");
  finished[finished_count] = (ThreadJob){0};
  logInfo("[THREADS] [getcomplete] fetched job (%d)", job.idx);
  logInfo("[THREADS] [getcomplete] finished: %d | pending: %d | processing: %d", finished_count, pending_count, processing_count);
  pthread_mutex_unlock(&m_finished);
  return job;
}

void threadJobComplete(ThreadJob job) {
  pthread_mutex_lock(&m_processing);
  ASSERT(processing_count > 0, "must exist jobs being processed");
  processing_count--;
  pthread_mutex_lock(&m_processing);

  pthread_mutex_lock(&m_finished);
  logInfo("[THREADS] [complete] completing job (%d)", job.tracker_id);
  ASSERT(job.callback, "a job must have a callback");
  ASSERT(job.results || job.result_code != 0, "to complete a job must have 'results' or an error 'result_code'");
  finished[finished_count] = job;
  job.idx = finished_count;
  finished_count++;
  ASSERT(finished_count < MAX_JOBS, "can't complete more jobs then MAX_JOBS");
  logInfo("[THREADS] [complete] %d finished jobs waiting processing and %d pending", finished_count, pending_count);
  pthread_mutex_unlock(&m_finished);
}

void threadJobEnqueue(ThreadJob job) {
  pthread_mutex_lock(&m_pending);
  logInfo("[THREADS] [create] creating job (%d)", job.tracker_id);
  ASSERT(job.callback, "a job must have a callback");
  job.idx = pending_count;
  pending[pending_count] = job;
  pending_count++;
  ASSERT(pending_count < MAX_JOBS, "can't schedule more jobs then MAX_JOBS");
  logInfo("[THREADS] [create] %d pending jobs waiting processing and %d finished", pending_count, finished_count);
  pthread_mutex_unlock(&m_pending);
}

void workerLoop(void *args) {
  logInfo("[THREADS] [process] start processing jobs");
  ASSERT(args == NULL, "this function should not receive any args right now");
  while (true) {
    logInfo("[THREADS] [process] wating for jobs");
    while (pending_count == 0) {
      struct timespec remaining, request = {5, 300 * NANOSECONDS_IN_MILLI};
      nanosleep(&request, &remaining);
    }

    u32 i = 0;
    if (pthread_mutex_lock(&m_pending) == 0) continue;
    logDebug("[THREADS] [process] %d jobs available for processing", pending_count);
    ThreadJob job = {0};
    for (i = 0; i < pending_count; i++) {
      if (threadJobIsZero(pending[i])) continue;
      job = jobMoveToProcesing(i);
      ASSERT(job.callback != NULL, "all initialized jobs should have callbacks assigned");
      break;
    }
    if (threadJobIsZero(job)) continue;
    logInfo("[THREADS] [process] processing job (%d)", i);
    pthread_mutex_unlock(&m_pending);
    job.result_code = job.callback(job.args, &job.results);
    threadJobComplete(job);
  }
}

u32 threadPoolInit() {
  u32 rc = 0;
  for (u32 i = 0; i < MAX_THREADS; i++) {
    rc = pthread_create(threads + i, NULL, (void *)workerLoop, NULL);
    if (rc > 0) {
      logInfo("[THREADS] [init] threadCreate failed: %d", rc);
      return rc;
    }
  }
  return rc;
}

u32 threadPoolDeinit() {
  u32 rc = 0;
  for (u32 i = 0; i < MAX_THREADS; i++) {
    rc = pthread_cancel(threads[i]);
    if (rc > 0) {
      logInfo("[THREADS] [deinit] threadWaitForExit failed: %d", rc);
      return rc;
    }
  }
  return rc;
}
