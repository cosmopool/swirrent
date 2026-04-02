#include <string.h>

#include "swirrent.h"

i32 main(i32 argc, char *argv[]) {
  SwirrentOptions options = {0};

  for (i32 i = 1; i < argc; i++) {
    if (i == 1) {
      if (!argv[i]) {
        printf("torrent file must be provided as argument!");
        exit(1);
      }
      options.torrent_path = argv[i];
      continue;
    }

    else if (strncmp(argv[i], "--load-response", 15) == 0 || strncmp(argv[i], "-l", 2) == 0) {
      if (options.raw_request_output_path) {
        printf("you must specify only one of those: '--load-response' or '--dump-response'");
        return 1;
      }
      i++;
      if (!argv[i]) {
        printf("raw request bin file must be provided as argument!");
        return 1;
      }
      options.raw_request_path = argv[i];
      continue;
    }

    else if (strncmp(argv[i], "--dump-response", 15) == 0 || strncmp(argv[i], "-d", 2) == 0) {
      if (options.raw_request_output_path) {
        printf("you must specify only one of those: '--load-response' or '--dump-response'");
        return 1;
      }
      i++;
      if (!argv[i]) {
        printf("output file for the response dump must be provided as argument!");
        exit(1);
      }
      options.dump_response = true;
      options.raw_request_output_path = argv[i];
      continue;
    }

    else if (strncmp(argv[i], "--decode-only", 13) == 0) {
      options.decode_only = true;
      continue;
    }

    else if (strncmp(argv[i], "-v", 2) == 0 || strncmp(argv[i], "--verbose", 9) == 0) {
      options.verbose = true;
      continue;
    }
  }

  SwirrentContext ctx = swirrentInit(options);
  swirrentMain(&ctx);
  swirrentShutdown(&ctx);
  return 0;
}
