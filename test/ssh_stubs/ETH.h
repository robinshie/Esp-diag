#pragma once
#include <cstdint>
struct FakeEthernet {bool up=true;bool linkUp(){return up;}uint32_t localIP(){return up?1:0;}};
extern FakeEthernet ETH;
