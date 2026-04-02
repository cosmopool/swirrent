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

#define STRING_IMPLEMENTATION
#include "core.h"
#include "swirrent.h"

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
      .torrent_path = "/torrent/e.torrent",
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
