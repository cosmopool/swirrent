#include "core.h"

typedef unsigned char InfoHash[20];
typedef char PeerID[20];

enum Event : u8 {
  STARTED,
  COMPLETED,
  STOPPED,
  EMPTY,
};

struct Tracker {
  /// The total amount uploaded so far, encoded in base ten ascii.
  u32 uploaded;

  /// The total amount downloaded so far, encoded in base ten ascii.
  u32 downloaded;

  /// The number of bytes this peer still has to download, encoded in base ten
  /// ascii. Note that this can't be computed from downloaded and the file
  /// length since it might be a resume, and there's a chance that some of the
  /// downloaded data failed an integrity check and had to be re-downloaded.

  u32 left;

  /// An optional parameter giving the IP (or dns name) which this peer is at.
  /// Generally used for the origin if it's on the same machine as the tracker.
  u8 ip[4];

  /// The port number this peer is listening on. Common behavior is for a
  /// downloader to try to listen on port 6881 and if that port is taken try
  /// 6882, then 6883, etc. and give up after 6889.
  u16 port;

  /// This is an optional key which maps to started, completed, or stopped (or
  /// empty, which is the same as not being present). If not present, this is
  /// one of the announcements done at regular intervals. An announcement using
  /// started is sent when a download first begins, and one using completed is
  /// sent when the download is complete. No completed is sent if the file was
  /// complete when started. Downloaders send an announcement using stopped when
  /// they cease downloading.
  Event event;

  /// The 20 byte sha1 hash of the bencoded form of the info value from the
  /// metainfo file. This value will almost certainly have to be escaped.
  /// Note that this is a substring of the metainfo file. The info-hash must be
  /// the hash of the encoded form as found in the .torrent file, which is
  /// identical to bdecoding the metainfo file, extracting the info dictionary
  /// and encoding it if and only if the bdecoder fully validated the input
  /// (e.g. key ordering, absence of leading zeros). Conversely that means
  /// clients must either reject invalid metainfo files or extract the substring
  /// directly. They must not perform a decode-encode roundtrip on invalid data.
  InfoHash info_hash;

  /// A string of length 20 which this downloader uses as its id. Each
  /// downloader generates its own id at random at the start of a new
  /// download. This value will also almost certainly have to be escaped.
  String peer_id;
};
