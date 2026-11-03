#pragma once
#include <cstdint>

// Firmware defaults and bounds shared by settings validation and routing.
namespace switchconfig {
constexpr unsigned MIN_FAILOVER_WAIT_SECONDS=1;
constexpr unsigned MAX_FAILOVER_WAIT_SECONDS=300;
constexpr unsigned DEFAULT_FAILOVER_WAIT_SECONDS=5;
constexpr uint64_t CONSOLE_IDLE_WAIT_MS=5000;
constexpr uint64_t SSH_RECONNECT_DEADLINE_MS=60000;
constexpr bool validFailoverWait(unsigned seconds){
  return seconds>=MIN_FAILOVER_WAIT_SECONDS&&seconds<=MAX_FAILOVER_WAIT_SECONDS;
}
constexpr uint64_t failoverWaitMs(unsigned seconds){
  return uint64_t(validFailoverWait(seconds)?seconds:DEFAULT_FAILOVER_WAIT_SECONDS)*1000;
}
}
