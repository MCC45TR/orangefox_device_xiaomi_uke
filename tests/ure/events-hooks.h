// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <linux/input.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cassert>
#include <deque>
#include <set>
inline unsigned devices=1,listing=0;
inline long modified=1,now_seconds=1;
inline bool fail_stat=false,hangup=false,relative_mouse=true;
inline unsigned closes=0,opens=0;
inline std::deque<input_event> queue;
inline int gr_fb_width() { return 2560; }
inline int gr_fb_height() { return 1440; }
inline DIR* fake_opendir(const char*) { listing=0; return reinterpret_cast<DIR*>(uintptr_t(1)); }
inline dirent* fake_readdir(DIR*) {
    static dirent entry{};
    if(listing>=devices)return nullptr;
    std::snprintf(entry.d_name,sizeof(entry.d_name),"event%u",listing++); return &entry;
}
inline int fake_dirfd(DIR*) { return 99; }
inline int fake_closedir(DIR*) { return 0; }
inline int fake_openat(int,const char*,int flags) { assert(flags&O_NONBLOCK); assert(flags&O_CLOEXEC); return int(++opens); }
inline int fake_open(const char*,int) { return -1; }
inline int fake_close(int) { ++closes; return 0; }
inline int fake_stat(const char*,struct stat* st) {
    if(fail_stat)return -1;
    *st={}; st->st_mtim.tv_sec=1; st->st_mtim.tv_nsec=modified; return 0;
}
inline int fake_gettimeofday(timeval* t,void*) { t->tv_sec=now_seconds; t->tv_usec=0; return 0; }
inline int fake_poll(pollfd* fds,nfds_t count,int) {
    for(nfds_t i=0;i<count;++i)fds[i].revents=0;
    if(!count)return 0;
    if(hangup) { fds[0].revents=POLLHUP; hangup=false; return 1; }
    if(!queue.empty()) { fds[0].revents=POLLIN; return 1; } return 0;
}
inline ssize_t fake_read(int,void* data,size_t size) {
    if(queue.empty())return -1;
    assert(size==sizeof(input_event)); std::memcpy(data,&queue.front(),size); queue.pop_front(); return ssize_t(size);
}
inline int fake_ioctl(int,unsigned long request,void* destination) {
    const unsigned number=_IOC_NR(request),size=_IOC_SIZE(request);
    if(number==6) { std::snprintf(static_cast<char*>(destination),size,"fixture USB HID"); return 16; }
    if(number>=0x20 && number<=0x20+EV_MAX) {
        std::memset(destination,0,size); auto* bits=static_cast<unsigned char*>(destination);
        const auto bit=[&](unsigned value) { assert(value/8<size); bits[value/8]|=uint8_t(1U<<(value%8)); };
        if(number==0x20) { bit(EV_KEY); if(relative_mouse)bit(EV_REL); }
        if(number==0x20+EV_REL && relative_mouse) { bit(REL_X); bit(REL_Y); }
        if(number==0x20+EV_KEY)bit(BTN_LEFT); // one-button mouse is valid
        return 0;
    }
    if(number>=0x40) { std::memset(destination,0,size); return 0; }
    return -1;
}
#define TW_NO_HAPTICS 1
template<class... T>inline void fake_log(T&&... arguments) { ((void)arguments,...); }
#define LOGI(...) fake_log(__VA_ARGS__)
#define LOGE(...) fake_log(__VA_ARGS__)
#define __unused __attribute__((unused))
#define opendir fake_opendir
#define readdir fake_readdir
#define dirfd fake_dirfd
#define closedir fake_closedir
#define openat fake_openat
#define open fake_open
#define close fake_close
#define stat(path, value) fake_stat(path, value)
#define gettimeofday fake_gettimeofday
#define poll fake_poll
#define read fake_read
#define ioctl fake_ioctl
