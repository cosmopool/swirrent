#include <pthread.h>
#include <sys/unistd.h>

#include "../core.h"
#include "../log.h"
#include "../threads.h"

static pthread_t threads[MAX_THREADS] = {0};
static i32 pending_count;
static i32 finished_count;
static ThreadJob pending[MAX_JOBS] = {0};
static ThreadJob finished[MAX_JOBS] = {0};
static pthread_mutex_t m_pending = {0};
static pthread_mutex_t m_finished = {0};

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
  pthread_mutex_lock(&m_finished);
  finished_count--;
  ThreadJob job = finished[finished_count];
  finished[finished_count] = (ThreadJob){0};
  pthread_mutex_unlock(&m_finished);
  logInfo("[THREADS] fetched %d", job.id);
  logInfo("[THREADS] %d finished jobs waiting processing", finished_count);
  return job;
}

void threadJobComplete(ThreadJob job) {
  logInfo("[THREADS] completing job (%d)", job.id);
  ASSERT(!job.processing, "a job cannot start with 'processing == true'. the thread that controls this value");
  ASSERT(job.callback, "a job must have a callback");
  ASSERT(job.results, "to complete a job the 'results' pointer must be not null");
  threadJobDestroy(job.id);
  pthread_mutex_lock(&m_finished);
  finished[finished_count] = job;
  finished_count++;
  pthread_mutex_unlock(&m_finished);
  logInfo("[THREADS] %d finished jobs waiting processing", finished_count);
}

void threadJobCreate(ThreadJob job) {
  logInfo("[THREADS] creating job (%d)", job.id);
  ASSERT(!job.processing, "a job cannot start with 'processing == true'. the thread that controls this value");
  ASSERT(job.callback, "a job must have a callback");
  job.id = pending_count;
  pthread_mutex_lock(&m_pending);
  pending[pending_count] = job;
  pending_count++;
  pthread_mutex_unlock(&m_pending);
  logInfo("[THREADS] %d pending jobs waiting processing", pending_count);
}

void threadJobDestroy(u32 idx) {
  logInfo("[THREADS] destroying job (%d)", idx);
  ASSERT(!pending[idx].processing, "should not destroy a task that is being processed.");
  pthread_mutex_lock(&m_pending);
  pending[idx] = pending[pending_count];
  pending[pending_count] = (ThreadJob){0};
  pending_count--;
  pthread_mutex_unlock(&m_pending);
  ASSERT(pending_count >= 0, "job count cannot be negative");
  logInfo("[THREADS] %d pending jobs waiting processing", pending_count);
}

void threadProcessJob(void *args) {
  logInfo("[THREADS] start processing jobs");
  ASSERT(args == NULL, "this function should not receive any args right now");
  while (true) {
    logInfo("[THREADS] wating for jobs");
    // (30 * NANOSECONDS_IN_MILLI
    while (pending_count == 0) {
      // static struct timespec remaining, request = {5, 100};
      // nanosleep(&request, &remaining);
      sleep(1);
    }

    i32 i = 0;
    if (pthread_mutex_lock(&m_pending) == 0) continue;
    logDebug("[THREADS] %d jobs available for processing", pending_count);
    for (i = 0; i < pending_count; i++) {
      if (isEmptyJob(i)) continue;
      if (pending[i].processing) continue;
      ASSERT(pending[i].callback, "all jobs should be initialized and callbacks assigned");
      pending[i].processing = true;
      break;
    }
    logInfo("[THREADS] processing job (%d)", i);
    pthread_mutex_unlock(&m_pending);
    pending[i].callback(pending[i].args, pending[i].results);
    pthread_mutex_lock(&m_pending);
    pending[i].processing = false;
    pthread_mutex_unlock(&m_pending);
    threadJobComplete(pending[i]);
  }
}

u32 threadInit() {
  u32 rc = 0;
  for (u32 i = 0; i < MAX_THREADS; i++) {
    rc = pthread_create(threads + i, NULL, (void *)threadProcessJob, NULL);
    if (rc > 0) {
      logInfo("[THREADS] threadCreate failed: %d", rc);
      return rc;
    }
    // logInfo("[THREADS] starting thread (%d): %d", i, threads[i].handle);
  }
  return rc;
}

u32 threadDeinit() {
  u32 rc = 0;
  for (u32 i = 0; i < MAX_THREADS; i++) {
    rc = pthread_cancel(threads[i]);
    if (rc > 0) {
      logInfo("[THREADS] threadWaitForExit failed: %d", rc);
      return rc;
    }
  }
  return rc;
}
