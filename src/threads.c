#include <switch.h>

#include "asio.h"
#include "core.h"
#include "threads.h"

static Thread threads[MAX_THREADS] = {0};
static u32 pending_count;
static u32 finished_count;
static ThreadJob pending[MAX_JOBS] = {0};
static ThreadJob finished[MAX_JOBS] = {0};
static Mutex m_pending = {0};
static Mutex m_finished = {0};

bool isEmptyJob(u32 i) {
  return pending[i].id == 0 &&
         pending[i].processing == false &&
         pending[i].callback == NULL;
}

bool threadJobIsEmpty(ThreadJob j) {
  return !j.args && !j.processing && !j.callback && !j.results && j.id == 0;
};

ThreadJob threadGetCompletedJob() {
  // ASSERT(!job.processing, "a job cannot start with 'processing == true'. the thread that controls this value");
  // ASSERT(job.callback, "a job must have a callback");
  // ASSERT(job.results, "to complete a job the 'results' pointer must be not null");
  mutexLock(&m_finished);
  finished_count--;
  ThreadJob job = finished[finished_count];
  finished[finished_count] = (ThreadJob){0};
  mutexUnlock(&m_finished);
  return job;
}

void threadJobComplete(ThreadJob job) {
  ASSERT(!job.processing, "a job cannot start with 'processing == true'. the thread that controls this value");
  ASSERT(job.callback, "a job must have a callback");
  ASSERT(job.results, "to complete a job the 'results' pointer must be not null");
  threadJobDestroy(job.id);
  mutexLock(&m_finished);
  finished[finished_count] = job;
  finished_count++;
  mutexUnlock(&m_finished);
}

void threadJobCreate(ThreadJob job) {
  ASSERT(!job.processing, "a job cannot start with 'processing == true'. the thread that controls this value");
  ASSERT(job.callback, "a job must have a callback");
  // ASSERT_VALID_FD(job.id);
  mutexLock(&m_pending);
  pending[pending_count] = job;
  pending_count++;
  mutexUnlock(&m_pending);
}

void threadJobDestroy(u32 idx) {
  ASSERT(!pending[idx].processing, "should not destroy a task that is being processed.");
  mutexLock(&m_pending);
  pending[idx] = pending[pending_count];
  pending[pending_count] = (ThreadJob){0};
  pending_count--;
  mutexUnlock(&m_pending);
  ASSERT(pending_count >= 0, "job count cannot be negative");
}

void threadProcessJob(void *args) {
  ASSERT(args == NULL, "this function should not receive any args right now");
  while (true) {
    while (pending_count == 0) {
      svcSleepThread(30 * NANOSECONDS_IN_MILLI);
    }

    u32 i = 0;
    if (mutexTryLock(&m_pending) == 0) continue;
    for (i = 0; i < pending_count; i++) {
      if (isEmptyJob(i)) continue;
      if (pending[i].processing) continue;
      ASSERT(pending[i].callback, "all jobs should be initialized and callbacks assigned");
      pending[i].processing = true;
      break;
    }
    mutexUnlock(&m_pending);
    pending[i].callback(pending[i].args, pending[i].results);
    mutexLock(&m_pending);
    pending->processing = false;
    mutexUnlock(&m_pending);
    threadJobComplete(pending[i]);
  }
}

Result threadInit() {
  u32 stack_sz = 64 * 1024;
  for (u32 i = 0; i < MAX_THREADS; i++) {
    threadCreate(threads + i, threadProcessJob, NULL, NULL, stack_sz, 0x20, 2);
  }
}
