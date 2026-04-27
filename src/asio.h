#pragma once

#include "core.h"
#include "torrent.h"

#define MAX_FD 1024

#define ASSERT_VALID_FD(fd)                         \
  ASSERT(fd > 0, "fd must be a positive integer."); \
  ASSERT(fd < MAX_FD, "the maximum fd size is" __STRING(MAX_FD) ".");

typedef enum {
  ASIO_NONE,
  ASIO_TIMEOUT,
  ASIO_READY,
} ASIO_STATUS;

typedef struct {
  i32 fd;
  void *args;
  void (*on_ready_callback)(i32 fd, u64 now, ASIO_STATUS, void *args);
  bool (*has_timeout_expired_callback)(i32 fd, u64 now);
} AsioFd;

void asioFdSet(AsioFd asio_fd);
void asioFdUnset(i32 fd);
void asioWaitForEvents(void);
void asioUnsetAll(void);
