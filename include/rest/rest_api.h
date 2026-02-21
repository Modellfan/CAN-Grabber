#ifndef REST_API_H
#define REST_API_H

#include <stdint.h>

namespace rest {

void init();
void start();
void stop();
void loop();
uint32_t last_activity_ms();
bool recently_active(uint32_t window_ms);

} // namespace rest

#endif // REST_API_H
