#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "log.h"

static FILE *logFile = NULL;
static bool should_log = true;

// log.c — add at top
#ifdef __SWITCH__
#include <switch/kernel/mutex.h>
static Mutex log_mutex = {0};
#else
#include <pthread.h>
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void logLock(void) {
#ifdef __SWITCH__
  mutexLock(&log_mutex);
#else
  pthread_mutex_lock(&log_mutex);
#endif
}

static void logUnlock(void) {
#ifdef __SWITCH__
  mutexUnlock(&log_mutex);
#else
  pthread_mutex_unlock(&log_mutex);
#endif
}

void logInit(bool enabled) {
  should_log = enabled;
};

void logSetOutputPath(const char *path) {
  if (logFile) fclose(logFile);
  logFile = fopen(path, "a");
  if (logFile) {
    setvbuf(logFile, NULL, _IONBF, 0);
  } else {
    perror("fopen");
  }
}

void logClose(void) {
  if (!logFile) return;
  fclose(logFile);
  logFile = NULL;
}

static void logWrite(const char *prefix, FILE *stream, const char *fmt, va_list args) {
  logLock();
  // Print to file with timestamp
  if (logFile) {
    fprintf(logFile, "[%ld] %s", time(NULL), prefix);
    va_list argsCopy;
    va_copy(argsCopy, args);
    vfprintf(logFile, fmt, argsCopy);
    va_end(argsCopy);
    fprintf(logFile, "\n");
    fflush(logFile);
  }

  // Print to stdout/stderr
  fprintf(stream, "%s", prefix);
  vfprintf(stream, fmt, args);
  fprintf(stream, "\n");
  fflush(stream);
  logUnlock();
}

void logInfo(const char *fmt, ...) {
  if (!should_log) return;
  va_list args;
  va_start(args, fmt);
  logWrite("", stdout, fmt, args);
  va_end(args);
}

void logError(const char *fmt, ...) {
  if (!should_log) return;
  va_list args;
  va_start(args, fmt);
  logWrite("[ERROR] ", stderr, fmt, args);
  va_end(args);
}

void logDebug(const char *fmt, ...) {
  if (!should_log) return;
  va_list args;
  va_start(args, fmt);
  logWrite("[DEBUG] ", stdout, fmt, args);
  va_end(args);
}
