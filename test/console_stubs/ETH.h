#pragma once
#include <cstdint>
struct FakeEthernet {bool up=true;uint32_t ip=0x010aA8c0;bool linkUp(){return up;}uint32_t localIP(){return ip;}};
static FakeEthernet ETH;
