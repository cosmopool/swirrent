// static char data[1024 * 1024] = {0};

// usize write_cb(char *ptr, usize size, usize nmemb, void *userdata) {
//   if (options->verbose) logInfo("----- RESPONSE SIZE: %ld", size * nmemb);
//   if (options->verbose) logInfo("----- RESPONSE DATA: %s", ptr);
//   if (options->dump_response) {
//     FILE *file = fopen(options->raw_request_output_path, "wb");
//     if (file) {
//       // Write some text to the file
//       size_t written = fwrite(ptr, 1, size * nmemb, file);
//       if (written < size * nmemb) {
//         logInfo("Warning: Only wrote %zu of %zu bytes.", written, size * nmemb);
//       }
//     } else {
//       perror("fopen");
//     }
//     // Close the file
//     fclose(file);
//   }
//
//   String *r = (String *)userdata;
//   r->len += size * nmemb;
//   memcpy((void *)r->data, ptr, r->len);
//   return size * nmemb;
// }

// TorrentTrackerResponse trackerHttpResponseDecode(String resp) {
//   TorrentTrackerResponse t_resp = {0};
//   torrentResponseDecode(&resp, &t_resp);
//
//   if (t_resp.warning_message.len > 0 && t_resp.peers.len == 0) {
//     logInfo("----- Skipping tracker with warning_message: %.*s. Trying another one.",
//             (u32)t_resp.warning_message.len, t_resp.warning_message.data);
//     return t_resp;
//   }
//   if (t_resp.failure_reason.len > 0 && t_resp.peers.len == 0) {
//     logInfo("----- Skipping tracker because failed: %.*s. Trying another one.",
//             (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
//     return t_resp;
//   }
//   logInfo("\n===| TRACKER RESPONSE");
//   logInfo("interval: %ld", t_resp.interval);
//   logInfo("min interval: %ld", t_resp.min_interval);
//   logInfo("complete: %ld", t_resp.complete);
//   logInfo("incomplete: %ld", t_resp.incomplete);
//   logInfo("downloaded: %ld", t_resp.downloaded);
//   logInfo("warning_message: %.*s", (u32)t_resp.warning_message.len, t_resp.warning_message.data);
//   logInfo("failure_reason: %.*s", (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
//   for (u32 i = 0; i < t_resp.peers6.count; i++) {
//     if (i == 0) logInfo("peers6:");
//     TorrentPeer6 peer = torrentPeer6Get(t_resp.peers6.data, i);
//     char ip_buff[INET6_ADDRSTRLEN] = {0};
//     if (!inet_ntop(AF_INET6, peer.ip.data, ip_buff, sizeof(ip_buff))) {
//       logInfo("  (%d)\t failed to parse ipv6: %s", i, strerror(errno));
//       continue;
//     }
//     logInfo("  (%d)\t ip: %s \t | port: %d", i, ip_buff, peer.port);
//   }
//
//   for (u32 i = 0; i < t_resp.peers.len / (IPV4_LEN + PORT_LEN); i++) {
//     if (i == 0) logInfo("peers:");
//     TorrentPeer peer = torrentPeerGet(t_resp.peers.data, i);
//     char ip_buff[INET_ADDRSTRLEN] = {0};
//     if (!inet_ntop(AF_INET, peer.ip.data, ip_buff, sizeof(ip_buff))) {
//       logInfo("  (%d)\t failed to parse ipv4: %s", i, strerror(errno));
//       continue;
//     }
//     logInfo("  (%d)\t ip: %s \t | port: %d", i, ip_buff, peer.port);
//   }
//   return t_resp;
// }

// TorrentTrackerResponse trackerHttpFetch(CURL *curl, String tracker_url, TorrentMetainfo *metainfo, u8 *peer_id) {
//   assert(tracker_url.data[0] == 'h');
//   assert(tracker_url.data[1] == 't');
//   assert(tracker_url.data[2] == 't');
//   assert(tracker_url.data[3] == 'p');
//
//   curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
//
//   usize offset = 0;
//   char url[1024] = {0};
//   offset += snprintf(url, tracker_url.len + 1, "%.*s", (i32)tracker_url.len, tracker_url.data);
//   switch (url[tracker_url.len - 1]) {
//   case '/':
//     assert(url[tracker_url.len] == '\0');
//     url[tracker_url.len - 1] = '?';
//     break;
//
//   default:
//     assert(url[tracker_url.len] == '\0');
//     url[tracker_url.len] = '?';
//     break;
//   }
//
//   usize hash_offset = 0;
//   char encoded_hash[61] = {0};
//   for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
//     hash_offset += sprintf(encoded_hash + hash_offset, "%%%02x", (u8)info_hash[i]);
//   }
//   offset += snprintf(url + offset, 1024 - offset, "info_hash=%s", encoded_hash);
//   offset += snprintf(url + offset, 1024 - offset, "&peer_id=%s", peer_id);
//   offset += snprintf(url + offset, 1024 - offset, "&port=%s", "6881");
//   offset += snprintf(url + offset, 1024 - offset, "&uploaded=%s", "0");
//   offset += snprintf(url + offset, 1024 - offset, "&downloaded=%s", "0");
//   if (info.is_single_file) {
//     offset += snprintf(url + offset, 1024 - offset, "&left=%ld", info.length);
//   } else {
//     usize len = 0;
//     for (u32 i = 0; i < info.multi_files.count; i++) {
//       len += info.multi_files.files[i].length;
//     }
//     offset += snprintf(url + offset, 1024 - offset, "&left=%ld", len);
//   }
//   offset += snprintf(url + offset, 1024 - offset, "&compact=1");
//
//   if (data[0] != '\0') memset(data, 0, 1024 * 1024);
//   String raw_resp = {.len = 0, .data = data};
//   curl_easy_setopt(curl, CURLOPT_URL, url);
//   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
//   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw_resp);
//   curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)10);
//   curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)10);
//   curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, (long)5);
//   curl_easy_setopt(curl, CURLOPT_ACCEPTTIMEOUT_MS, (long)10000);
//
//   TorrentTrackerResponse resp = {0};
//   i32 result = curl_easy_perform(curl);
//   if (result != CURLE_OK) {
//     logError("----- Communication with tracker an error occurred: %s\n", curl_easy_strerror(result));
//     return resp;
//   }
//   return trackerHttpResponseDecode(raw_resp);
// }

// void trackerOptionsSet(SwirrentOptions *op) {
//   options = op;
// }
// static char data[1024 * 1024] = {0};

// usize write_cb(char *ptr, usize size, usize nmemb, void *userdata) {
//   if (options->verbose) logInfo("----- RESPONSE SIZE: %ld", size * nmemb);
//   if (options->verbose) logInfo("----- RESPONSE DATA: %s", ptr);
//   if (options->dump_response) {
//     FILE *file = fopen(options->raw_request_output_path, "wb");
//     if (file) {
//       // Write some text to the file
//       size_t written = fwrite(ptr, 1, size * nmemb, file);
//       if (written < size * nmemb) {
//         logInfo("Warning: Only wrote %zu of %zu bytes.", written, size * nmemb);
//       }
//     } else {
//       perror("fopen");
//     }
//     // Close the file
//     fclose(file);
//   }
//
//   String *r = (String *)userdata;
//   r->len += size * nmemb;
//   memcpy((void *)r->data, ptr, r->len);
//   return size * nmemb;
// }

// TorrentTrackerResponse trackerHttpResponseDecode(String resp) {
//   TorrentTrackerResponse t_resp = {0};
//   torrentResponseDecode(&resp, &t_resp);
//
//   if (t_resp.warning_message.len > 0 && t_resp.peers.len == 0) {
//     logInfo("----- Skipping tracker with warning_message: %.*s. Trying another one.",
//             (u32)t_resp.warning_message.len, t_resp.warning_message.data);
//     return t_resp;
//   }
//   if (t_resp.failure_reason.len > 0 && t_resp.peers.len == 0) {
//     logInfo("----- Skipping tracker because failed: %.*s. Trying another one.",
//             (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
//     return t_resp;
//   }
//   logInfo("\n===| TRACKER RESPONSE");
//   logInfo("interval: %ld", t_resp.interval);
//   logInfo("min interval: %ld", t_resp.min_interval);
//   logInfo("complete: %ld", t_resp.complete);
//   logInfo("incomplete: %ld", t_resp.incomplete);
//   logInfo("downloaded: %ld", t_resp.downloaded);
//   logInfo("warning_message: %.*s", (u32)t_resp.warning_message.len, t_resp.warning_message.data);
//   logInfo("failure_reason: %.*s", (u32)t_resp.failure_reason.len, t_resp.failure_reason.data);
//   for (u32 i = 0; i < t_resp.peers6.count; i++) {
//     if (i == 0) logInfo("peers6:");
//     TorrentPeer6 peer = torrentPeer6Get(t_resp.peers6.data, i);
//     char ip_buff[INET6_ADDRSTRLEN] = {0};
//     if (!inet_ntop(AF_INET6, peer.ip.data, ip_buff, sizeof(ip_buff))) {
//       logInfo("  (%d)\t failed to parse ipv6: %s", i, strerror(errno));
//       continue;
//     }
//     logInfo("  (%d)\t ip: %s \t | port: %d", i, ip_buff, peer.port);
//   }
//
//   for (u32 i = 0; i < t_resp.peers.len / (IPV4_LEN + PORT_LEN); i++) {
//     if (i == 0) logInfo("peers:");
//     TorrentPeer peer = torrentPeerGet(t_resp.peers.data, i);
//     char ip_buff[INET_ADDRSTRLEN] = {0};
//     if (!inet_ntop(AF_INET, peer.ip.data, ip_buff, sizeof(ip_buff))) {
//       logInfo("  (%d)\t failed to parse ipv4: %s", i, strerror(errno));
//       continue;
//     }
//     logInfo("  (%d)\t ip: %s \t | port: %d", i, ip_buff, peer.port);
//   }
//   return t_resp;
// }

// TorrentTrackerResponse trackerHttpFetch(CURL *curl, String tracker_url, TorrentMetainfo *metainfo, u8 *peer_id) {
//   assert(tracker_url.data[0] == 'h');
//   assert(tracker_url.data[1] == 't');
//   assert(tracker_url.data[2] == 't');
//   assert(tracker_url.data[3] == 'p');
//
//   curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
//
//   usize offset = 0;
//   char url[1024] = {0};
//   offset += snprintf(url, tracker_url.len + 1, "%.*s", (i32)tracker_url.len, tracker_url.data);
//   switch (url[tracker_url.len - 1]) {
//   case '/':
//     assert(url[tracker_url.len] == '\0');
//     url[tracker_url.len - 1] = '?';
//     break;
//
//   default:
//     assert(url[tracker_url.len] == '\0');
//     url[tracker_url.len] = '?';
//     break;
//   }
//
//   usize hash_offset = 0;
//   char encoded_hash[61] = {0};
//   for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
//     hash_offset += sprintf(encoded_hash + hash_offset, "%%%02x", (u8)info_hash[i]);
//   }
//   offset += snprintf(url + offset, 1024 - offset, "info_hash=%s", encoded_hash);
//   offset += snprintf(url + offset, 1024 - offset, "&peer_id=%s", peer_id);
//   offset += snprintf(url + offset, 1024 - offset, "&port=%s", "6881");
//   offset += snprintf(url + offset, 1024 - offset, "&uploaded=%s", "0");
//   offset += snprintf(url + offset, 1024 - offset, "&downloaded=%s", "0");
//   if (info.is_single_file) {
//     offset += snprintf(url + offset, 1024 - offset, "&left=%ld", info.length);
//   } else {
//     usize len = 0;
//     for (u32 i = 0; i < info.multi_files.count; i++) {
//       len += info.multi_files.files[i].length;
//     }
//     offset += snprintf(url + offset, 1024 - offset, "&left=%ld", len);
//   }
//   offset += snprintf(url + offset, 1024 - offset, "&compact=1");
//
//   if (data[0] != '\0') memset(data, 0, 1024 * 1024);
//   String raw_resp = {.len = 0, .data = data};
//   curl_easy_setopt(curl, CURLOPT_URL, url);
//   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
//   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw_resp);
//   curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)10);
//   curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)10);
//   curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, (long)5);
//   curl_easy_setopt(curl, CURLOPT_ACCEPTTIMEOUT_MS, (long)10000);
//
//   TorrentTrackerResponse resp = {0};
//   i32 result = curl_easy_perform(curl);
//   if (result != CURLE_OK) {
//     logError("----- Communication with tracker an error occurred: %s\n", curl_easy_strerror(result));
//     return resp;
//   }
//   return trackerHttpResponseDecode(raw_resp);
// }

// void trackerOptionsSet(SwirrentOptions *op) {
//   options = op;
// }

