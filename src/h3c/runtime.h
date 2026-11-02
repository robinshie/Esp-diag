#pragma once
// Compatibility alias; Runtime now requires explicit device/strategy composition.
#include "bdi_agent.h"
#include "loop_discovery.h"
#include "../adapters/h3c/device_adapter.h"
#include "../config/site_policy.h"
#include "../application/switch_runtime.h"
namespace h3c { using Runtime=switchapp::SwitchRuntime; }
