#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>
class String {
  std::string value;
public:
  String()=default;
  String(const char* s):value(s){}
  String(const std::string& s):value(s){}
  template<class T,typename std::enable_if<std::is_integral<T>::value,int>::type=0>
  String(T n):value(std::to_string(n)){}
  const char* c_str()const{return value.c_str();}
  friend String operator+(const String& a,const String& b){return a.value+b.value;}
};
struct FakeQueue {size_t capacity,itemBytes;std::deque<std::vector<uint8_t>> values;};
using QueueHandle_t=FakeQueue*;
using SemaphoreHandle_t=bool*;
extern std::vector<FakeQueue*> fakeQueues;
extern std::vector<bool*> fakeLocks;
extern uint32_t fakeMillis;
extern std::function<void()> onDelay;
extern void (*journalWorker)(void*);
extern void* journalArg;
struct StopJournal {};
constexpr unsigned portMAX_DELAY=~0u;
constexpr int pdTRUE=1,pdFALSE=0,pdPASS=1;
#define pdMS_TO_TICKS(ms) (ms)
inline uint32_t millis(){return fakeMillis;}
inline QueueHandle_t xQueueCreate(unsigned count,unsigned bytes){auto q=new FakeQueue{count,bytes,{}};fakeQueues.push_back(q);return q;}
inline int xQueueSend(QueueHandle_t q,const void* p,unsigned){if(q->values.size()>=q->capacity)return pdFALSE;auto bytes=static_cast<const uint8_t*>(p);q->values.emplace_back(bytes,bytes+q->itemBytes);return pdTRUE;}
inline int xQueueReceive(QueueHandle_t q,void* p,unsigned){if(q->values.empty())return pdFALSE;memcpy(p,q->values.front().data(),q->itemBytes);q->values.pop_front();return pdTRUE;}
inline SemaphoreHandle_t xSemaphoreCreateMutex(){auto lock=new bool(false);fakeLocks.push_back(lock);return lock;}
inline int xSemaphoreTake(SemaphoreHandle_t lock,unsigned){if(*lock)return pdFALSE;*lock=true;return pdTRUE;}
inline void xSemaphoreGive(SemaphoreHandle_t lock){*lock=false;}
inline int xTaskCreate(void (*fn)(void*),const char*,unsigned,void* arg,unsigned,void*){journalWorker=fn;journalArg=arg;return pdPASS;}
inline void vTaskDelay(unsigned ms){fakeMillis+=ms;if(onDelay)onDelay();}
inline void vTaskDelete(void*){throw StopJournal();}
