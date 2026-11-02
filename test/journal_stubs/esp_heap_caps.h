#pragma once
#include <cstdlib>
#include <vector>
constexpr unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
extern std::vector<void*> fakeAllocations;
inline void* heap_caps_calloc(size_t n,size_t size,unsigned){auto p=calloc(n,size);fakeAllocations.push_back(p);return p;}
