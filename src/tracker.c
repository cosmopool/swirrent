
#include "tracker.h"
#include "bencode.h"
#include "core.h"

void trackerResponseDecode(BencodeParser *p, TrackerResponse *resp) {
  if (p->bencode[p->cursor] != 'd') {
    resp->failure_reason = (String){.data = p->bencode, .len = p->bencode_len};
    return;
  }

  p->cursor++;
  while (p->bencode[p->cursor] != 'e') {
    String key = bencodeStringDecode(p);

    if (STRING_MATCHES("failure reason", key)) {
      resp->failure_reason = bencodeStringDecode(p);
      return;
    }

    else if (STRING_MATCHES("complete", key)) {
      resp->complete = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("downloaded", key)) {
      resp->downloaded = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("incomplete", key)) {
      resp->incomplete = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("interval", key)) {
      resp->interval = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("min interval", key)) {
      resp->min_interval = bencodeIntegerDecode(p);
      continue;
    }

    else if (STRING_MATCHES("peers", key)) {
      if (p->bencode[p->cursor] == 'l') {
        bencodeValueSkip(p);
        printf("----- decoding peers: only compact form decoding is implemented yet.\n");
        continue;
      }

      String str = bencodeStringDecode(p);
      if (str.len % (IPV4_LEN + PORT_LEN) != 0) {
        printf("----- decoding peers4: invalid length (must be multiple of 10).\n");
        continue;
      }

      resp->peers = (TorrentPeers){
          .data = str.data,
          .len = str.len,
          .count = str.len / (IPV4_LEN + PORT_LEN),
      };
      continue;
    }

    else if (STRING_MATCHES("peers6", key)) {
      if (p->bencode[p->cursor] == 'l') {
        bencodeValueSkip(p);
        printf("----- decoding peers6: only compact form decoding is implemented yet.\n");
        continue;
      }

      printf("----- decoding peers6: nintendo switch does not support ipv6.\n");
      bencodeValueSkip(p);
      continue;

      String str = bencodeStringDecode(p);
      if (str.len % (IPV6_LEN + PORT_LEN) != 0) {
        printf("----- decoding peers6: invalid length (must be multiple of 18).\n");
        continue;
      }

      resp->peers6 = (TorrentPeers6){
          .data = str.data,
          .len = str.len,
          .count = str.len / (IPV6_LEN + PORT_LEN),
      };
      continue;
    }

    else if (STRING_MATCHES("warning message", key)) {
      resp->warning_message = bencodeStringDecode(p);
      continue;
    }

    // skip unrecognized/unwanted key/values
    printf("-> |%.*s\n", (u32)key.len, key.data);
    bencodeValueSkip(p);
    continue;
  }
  p->cursor++;
}
