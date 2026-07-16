#ifdef RX_LOAD_TEST

#include "upload/upload_manager.h"

namespace upload {

// The load test validates the CAN-to-SD path only. Closing a test file must not
// start the unrelated network upload pipeline.
void request_upload_auto(const char*) {}

} // namespace upload

#endif // RX_LOAD_TEST
