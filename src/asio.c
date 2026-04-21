#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "asio.h"
#include "core.h"
#include "log.h"

static usize num_pfds = 0;
static struct pollfd pfds[MAX_FD] = {0};
static AsioFd pfds_ctx[MAX_FD] = {0};

void asioFdSet(AsioFd asio) {
  ASSERT_VALID_FD(asio.fd);
  pfds[num_pfds] = (struct pollfd){.fd = asio.fd, .events = POLLIN | POLLHUP};
  pfds_ctx[num_pfds] = asio;
  num_pfds++;
}

void asioFdUnset(i32 fd) {
  ASSERT_VALID_FD(fd);
  close(fd);
  for (u32 i = 0; i < num_pfds; i++) {
    if (pfds[i].fd != fd) continue;
    pfds[i] = (struct pollfd){0};
    pfds_ctx[i] = (AsioFd){0};
    num_pfds--;
    return;
  }
}

void asioUnsetAll() {
  isize remaning = num_pfds;
  for (i32 i = 0; i < MAX_FD; i++) {
    if (remaning < 0) return;
    if (pfds[i].fd <= 0) continue;
    asioFdUnset(i);
    remaning--;
  }
}

void asioWaitForEvents() {
  i32 poll_count;
  struct timespec ts;
  while ((poll_count = poll(pfds, num_pfds, 15000)) >= 0) {
    if (poll_count == -1) {
      perror("poll");
      exit(1);
    }
    logInfo("[ASIO] pending pfds: %lu, ready: %d", num_pfds, poll_count);
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
      logError("[TRACKER] error fetching current time: %s", strerror(errno));
      continue;
    }
    u64 now = ts.tv_sec;

    // Run through connections looking for data to read
    for (u32 i = 0; i <= num_pfds; i++) {
      u32 fd = pfds[i].fd;
      bool is_empty_pfd = fd <= 0 && pfds[i].revents == 0 && pfds[i].events == 0;
      if (is_empty_pfd) continue;

      bool has_callback = pfds_ctx[i].on_ready_callback != NULL;
      if (!has_callback) continue;

      if (pfds[i].revents & (pfds[i].events)) {
        pfds_ctx[i].on_ready_callback(fd, now, ASIO_READY, pfds_ctx[i].args);
        continue;
      }

      if (pfds_ctx[i].has_timeout_expired_callback(fd, now)) {
        pfds_ctx[i].on_ready_callback(fd, now, ASIO_TIMEOUT, pfds_ctx[i].args);
        continue;
      }
    }
  }
}
