#pragma once
#include <cstdint>
extern uint32_t fakeMillis;
inline int64_t esp_timer_get_time(){return int64_t(fakeMillis)*1000;}
