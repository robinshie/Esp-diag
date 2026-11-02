#pragma once
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>
using SemaphoreHandle_t=void*;
constexpr unsigned portMAX_DELAY=~0u;
constexpr int pdPASS=1;
#define pdMS_TO_TICKS(ms) (ms)
extern uint32_t fakeMillis;
extern std::function<void()> onDelay;
extern void (*worker)(void*);
extern void* workerArg;
extern std::string serialOutput;
inline uint32_t millis(){return fakeMillis;}
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
inline void xSemaphoreTake(SemaphoreHandle_t,unsigned){}
inline void xSemaphoreGive(SemaphoreHandle_t){}
inline int xTaskCreate(void (*fn)(void*),const char*,unsigned,void* arg,unsigned,void*){worker=fn;workerArg=arg;return pdPASS;}
inline void vTaskDelay(unsigned ms){fakeMillis+=ms;onDelay();}
struct FakeSerial {void printf(const char* format,...){char buffer[256];va_list args;va_start(args,format);vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);serialOutput+=buffer;}};
extern FakeSerial Serial;
