#pragma once
#if ESP_LOG_AI_EXPERIMENT
#include <Arduino.h>
#include "../experiments/tiny_log_explainer/inference.h"
// Classifies text for display. Has no reference to runtime, settings, or action APIs.
inline String tinyLogReply(const String& text){
  const auto p=tinylog::predict(std::string(text.c_str(),text.length()));
  char reply[256];
  snprintf(reply,sizeof(reply),"{\"experimental\":true,\"model\":\"%s\",\"label\":\"%s\",\"score\":%.6f,\"threshold\":%.2f,\"accepted\":%s,\"truncated\":%s}",
    tinylog::VERSION,tinylog::LABELS[p.category],double(p.score),double(tinylog::THRESHOLD),p.accepted?"true":"false",p.truncated?"true":"false");
  return String(reply);
}
#endif
