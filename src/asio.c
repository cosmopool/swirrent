#include <assert.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "asio.h"
#include "core.h"

static usize num_pfds = 0;
static struct pollfd pfds[MAX_FD] = {0};
static AsioFd pfds_ctx[MAX_FD] = {0};

void asioFdSet(AsioFd asio) {
  ASSERT_VALID_FD(asio.fd);
  pfds[asio.fd] = (struct pollfd){.fd = asio.fd, .events = POLLIN | POLLHUP};
  pfds_ctx[asio.fd] = asio;
  num_pfds++;
}

void asioFdUnset(i32 fd) {
  ASSERT_VALID_FD(fd);
  close(fd);
  pfds[fd] = (struct pollfd){0};
  pfds_ctx[fd] = (AsioFd){0};
  num_pfds--;
}

void asioWaitForEvents(TorrentMetainfo *m, u8 id[20]) {
  u32 tries = 0;
  i32 poll_count;
  while ((poll_count = poll(pfds, num_pfds, 1000)) >= 0) {
    if (poll_count == -1) {
      perror("poll");
      exit(1);
    }
    printf("pending pfds: %lu, ready: %d\n", num_pfds, poll_count);
    if (poll_count == 0) {
      if (tries > 2) return;
      tries++;
      continue;
    }

    // Run through connections looking for data to read
    for (i32 i = 0; i <= MAX_FD; i++) {
      i32 fd = pfds[i].fd;
      if (fd <= 0 && pfds[i].revents == 0 && pfds[i].events == 0) continue;

      bool is_ready_to_read = pfds[i].revents & (pfds[i].events);
      if (!is_ready_to_read) continue;

      bool has_callback = pfds_ctx[fd].on_ready_callback != NULL;
      if (!has_callback) continue;

      pfds_ctx[fd].on_ready_callback(fd, m, id);
    }
  }
}
