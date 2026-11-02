#pragma once
constexpr unsigned HSPI=1;
class SPIClass {public:explicit SPIClass(unsigned){} bool begin(int,int,int,int){return true;}};
