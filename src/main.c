#include "bencode.h"
#include <stdio.h>

// Include the main libnx system header, for Switch development
#include <switch.h>

#include <curl/curl.h>

#define ARENA_IMPLEMENTATION
#include "arena.h"

static Arena default_arena = {0};

void network_request(void) {
  CURL *curl;
  CURLcode res;

  printf("curl init\n");
  curl_global_init(CURL_GLOBAL_DEFAULT);

  BencodeReturn_t result = bencode_decode(&default_arena, "i15e", 4);
  (void)result;
  curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, "https://example.com/");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libnx curl example/1.0");
    // Add any other options here.

    printf("curl_easy_perform\n");
    consoleUpdate(NULL);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
      printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

    // In an actual app you should return an error on failure, following
    // cleanup.

    printf("cleanup\n");
    curl_easy_cleanup(curl);
  }

  curl_global_cleanup();
}

// Main program entrypoint
int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  // This example uses a text console, as a simple way to output text to the
  // screen. If you want to write a software-rendered graphics application,
  //   take a look at the graphics/simplegfx example, which uses the libnx
  //   Framebuffer API instead.
  // If on the other hand you want to write an OpenGL based application,
  //   take a look at the graphics/opengl set of examples, which uses EGL
  //   instead.
  consoleInit(NULL);

  // Configure our supported input layout: a single player with standard
  // controller styles
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  //
  // Initialize the default gamepad (which reads handheld mode inputs as well
  //
  // the first connected controller)
  PadState pad;
  padInitializeDefault(&pad);

  socketInitializeDefault();

  printf("curl example\n");

  // network_request();
  // allocate 15MB
  arena_alloc(&default_arena, 1024 * 1024 * 15);
  arena_reset(&default_arena);
  BencodeReturn_t result = bencode_decode(&default_arena, "i15e", 4);
  (void)result;

  // Main loop
  while (appletMainLoop()) {
    // Scan the gamepad. This should be done once for each frame
    padUpdate(&pad);

    // padGetButtonsDown returns the set of buttons that have been
    // newly pressed in this frame compared to the previous one
    u64 kDown = padGetButtonsDown(&pad);

    if (kDown & HidNpadButton_Plus) break; // break in order to return to hbmenu

    // Your code goes here

    // Update the console, sending a new frame to the display
    consoleUpdate(NULL);
  }

  socketExit();
  // Deinitialize and clean up resources used by the console (important!)
  consoleExit(NULL);
  return 0;
}
