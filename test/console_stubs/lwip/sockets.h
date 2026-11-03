#pragma once
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <string>
namespace wire {
static int connects=0,closes=0,connectError=EINPROGRESS,soError=0,selection=1;
static bool eof=false;
static std::string input,output;
static int socket(int,int,int){return 3;}
static int fcntl(int,int,int){return 0;}
static int close(int){++closes;input.clear();return 0;}
static int bind(int,const sockaddr* p,socklen_t){assert(reinterpret_cast<const sockaddr_in*>(p)->sin_addr.s_addr==ETH.ip);return 0;}
static int connect(int,const sockaddr* p,socklen_t){assert(ntohs(reinterpret_cast<const sockaddr_in*>(p)->sin_port)==4001);++connects;errno=connectError;return connectError?-1:0;}
static int select(int,fd_set*,fd_set*,fd_set*,timeval* t){assert(t->tv_sec==0&&t->tv_usec==0);return selection;}
static int getsockopt(int,int,int,void* p,socklen_t*){*static_cast<int*>(p)=soError;return 0;}
static int recv(int,void* p,size_t n,int){if(eof)return 0;if(input.empty()){errno=EAGAIN;return -1;}n=std::min(n,input.size());input.copy(static_cast<char*>(p),n);input.erase(0,n);return int(n);}
static int send(int,const void* p,size_t n,int){n=std::min(n,size_t(2));output.append(static_cast<const char*>(p),n);return int(n);}
}
#define socket wire::socket
#define fcntl wire::fcntl
#define close wire::close
#define bind wire::bind
#define connect wire::connect
#define select wire::select
#define getsockopt wire::getsockopt
#define recv wire::recv
#define send wire::send
