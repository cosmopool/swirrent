#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 16
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 4
#endif

#include "core.h"
#include "swirrent.h"

void swirrentPrintMemoryUtilization(u64 *total, u64 *used) {
  svcGetInfo(total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
  printf("total: %lu | used: %lu\n", *total / 1024, *used / 1024);
}

int main() {
  // switch basic init
  consoleInit(NULL);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  Result rc = socketInitializeDefault();
  if (R_FAILED(rc)) {
    printf("socketInitializeDefault failed: %08X\n", rc);
    return 1;
  }

  SwirrentOptions options = {
      .verbose = true,
      .torrent_path = "/torrents/e.torrent",
      .log_output_path = "/torrents/log.txt",
  };
  SwirrentContext ctx = swirrentInit(options);

  while (appletMainLoop()) {
    padUpdate(&pad);
    u64 kDown = padGetButtonsDown(&pad);
    if (kDown & HidNpadButton_Minus) break;

    // Your code goes here
    if (kDown & HidNpadButton_Plus) {
      i32 r = swirrentMain(&ctx);
      printf("swirrent result: %d\n", r);
    }

    // Update the console, sending a new frame to the display
    consoleUpdate(NULL);
  }

  swirrentShutdown(&ctx);
  socketExit();
  consoleExit(NULL);
  return 0;
}
