#ifndef UPLOAD_MANAGER_H
#define UPLOAD_MANAGER_H

#include <stddef.h>
#include <stdint.h>

namespace upload {

struct Stats {
  bool initialized;
  bool uploading;
  uint32_t upload_speed_bytes_per_sec;
  uint32_t current_file_size_bytes;
  uint32_t current_file_sent_bytes;
  uint32_t total_uploaded_files;
  uint64_t total_uploaded_bytes;
  uint32_t uploaded_files;
  uint32_t outstanding_files;
  uint64_t outstanding_bytes;
  bool last_error;
  bool last_error_interrupted;
  bool last_error_connect;
  char last_error_message[96];
  bool server_reachable_known;
  bool server_reachable;
  int32_t server_rtt_ms;
  char server_reach_message[96];
};

void init();
void loop();
void request_upload(const char* path);
void request_upload_auto(const char* path);
void queue_pending();
Stats get_stats();

} // namespace upload

#endif // UPLOAD_MANAGER_H
