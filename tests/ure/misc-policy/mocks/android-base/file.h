#pragma once
#include <fixture-external.hpp>
#include <cerrno>
namespace android::base {
inline bool ReadFully(int fd, void* data, std::size_t size) {
    auto* bytes=static_cast<char*>(data);
    while(size) { auto n=::read(fd,bytes,size); if(n<0 && errno==EINTR) continue; if(n<=0) return false; bytes+=n; size-=static_cast<std::size_t>(n); }
    return true;
}
inline bool WriteFully(int fd, const void* data, std::size_t size) {
    auto* bytes=static_cast<const char*>(data);
    while(size) { auto n=::write(fd,bytes,size); if(n<0 && errno==EINTR) continue; if(n<=0) return false; bytes+=n; size-=static_cast<std::size_t>(n); }
    return true;
}
}
