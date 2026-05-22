#pragma once

#include "core.h"
#include "torrent.h"

#define MAX_FD 1024

#define ASIOIN 0x0001      /* any readable data available */
#define ASIOPRI 0x0002     /* OOB/Urgent readable data */
#define ASIOOUT 0x0004     /* file descriptor is writeable */
#define ASIORDNORM 0x0040  /* non-OOB/URG data available */
#define ASIOWRNORM ASIOOUT /* no write type differentiation */
#define ASIORDBAND 0x0080  /* OOB/Urgent readable data */
#define ASIOWRBAND 0x0100  /* OOB/Urgent data can be written */

/*
 * FreeBSD extensions: polling on a regular file might return one
 * of these events (currently only supported on local filesystems).
 */
#define ASIOEXTEND 0x0200 /* file may have been extended */
#define ASIOATTRIB 0x0400 /* file attributes may have changed */
#define ASIONLINK 0x0800  /* (un)link/rename may have happened */
#define ASIOWRITE 0x1000  /* file's contents may have changed */

/*
 * These events are set if they occur regardless of whether they were
 * requested.
 */
#define ASIOERR 0x0008  /* some poll error occurred */
#define ASIOHUP 0x0010  /* file descriptor was "hung up" */
#define ASIONVAL 0x0020 /* requested events "invalid" */

#define ASIOSTANDARD (ASIOIN | ASIOPRI | ASIOOUT | ASIORDNORM | ASIORDBAND | \
                      ASIOWRBAND | ASIOERR | ASIOHUP | ASIONVAL)

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
  u16 events;
} AsioFd;

void asioFdSet(AsioFd asio_fd);
void asioFdUnset(i32 fd);
void asioWaitForEvents(void);
void asioUnsetAll(void);
