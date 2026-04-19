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
  void (*data_callback)();
  void (*on_ready_callback)(i32 fd, void *metainfo, u8 peer_id[20], u64 now, ASIO_STATUS);
  bool (*has_timeout_expired_callback)(i32 fd, u64 now);
} AsioFd;

void asioFdSet(AsioFd asio_fd);
void asioFdUnset(i32 fd);
void asioWaitForEvents(TorrentMetainfo *m, u8 id[20]);
void asioUnsetAll();
